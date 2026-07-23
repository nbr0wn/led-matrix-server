// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Multi-mode animation app for the rpi-rgb-led-matrix library, controlled over
// HTTP: a browser web UI plus a JSON API (POST /api) for scripting.
//
// Modeled after the programs in ../examples-api-use/. Rendering uses an
// off-screen FrameCanvas with SwapOnVSync() for smooth double buffering.
//
// Panel geometry, GPIO options, web-server port, and everything else are read
// from led-matrix-server.conf (next to the binary); there are no CLI arguments.
//
// This code is public domain
// (but note that the led-matrix library this depends on is GPL v2).

#include "common/mode.h"
#include "modules/modules.h"
#include "modules/json.h"        // read-only JSON reader, for the /api endpoint

#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <atomic>
#include <errno.h>
#include <map>
#include <math.h>
#include <mutex>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifdef USE_TLS                      // -DUSE_TLS from `make TLS=1`; links OpenSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using rgb_matrix::RGBMatrix;
using rgb_matrix::FrameCanvas;
using rgb_matrix::Canvas;

// --------------------------------------------------------------------------
// Globals shared between the render thread and the control server thread.
// --------------------------------------------------------------------------
volatile bool interrupt_received = false;
static std::atomic<int> g_current_mode(0);       // index into the mode list
std::atomic<bool> g_running(true);
static std::atomic<bool> g_cycle(false);         // mode 0: auto-cycle all modes
static std::atomic<int> g_cycle_secs(15);        // seconds per mode while cycling
int g_video_w = 256, g_video_h = 144;     // video-mode source res; computed from the panel in main()
static int g_web_port = 8080;                      // HTTP control server port (-w)
static std::string g_web_auth;                     // "user:pass" for HTTP basic auth (-A); empty = off
static std::string g_tls_cert, g_tls_key;          // -c/-k PEM paths; both set => serve HTTPS
#ifdef USE_TLS
static SSL_CTX *g_ssl_ctx = nullptr;               // non-null once a cert+key are loaded
#endif
static std::vector<std::string> g_mode_names;    // filled in main()
// Which modes the auto-cycler steps through, one entry per mode. This used to
// be a uint64 bitmask; fetched modules push the mode count past 64, at which
// point the tail of the list silently could not be cycled. Guarded by
// g_modes_mtx, like the mode list it indexes.
static std::vector<char> g_checked;
static std::map<std::string, std::string> g_mode_args;  // persisted per-mode arguments (by name)
static std::mutex g_args_mtx;                    // guards g_mode_args
// Guards the mode list (g_modes / g_mode_names), which the render thread
// rebuilds whenever fetched modules are installed or removed. Recursive
// because the locked read paths call each other (e.g. selection -> save).
static std::recursive_mutex g_modes_mtx;
static size_t g_nbuiltin = 0;                    // modes above this index are fetched

static void InterruptHandler(int signo) {
  interrupt_received = true;
  g_running.store(false);
}

static std::vector<Mode *> *g_modes = nullptr;   // for routing args to modes

// --------------------------------------------------------------------------
// Cycle config: which modes the auto-cycler steps through (checkable in the web
// UI) plus the seconds-per-mode, persisted to ~/led-matrix-server/cycle.conf. Format
// is a "@secs=<n>" line followed by one checked mode name per line. An empty
// mode set falls back to "all" so the cycler is never a no-op.
// --------------------------------------------------------------------------
static std::string CyclePath() {
  const char *h = getenv("HOME");
  return std::string(h ? h : "/root") + "/led-matrix-server/cycle.conf";
}
// Resize the checked set to match the mode list, defaulting new entries to `on`.
static void FitChecked(bool on) {
  g_checked.resize(g_mode_names.size(), on ? 1 : 0);
}
static bool AnyChecked() {
  for (size_t i = 0; i < g_checked.size(); ++i) if (g_checked[i]) return true;
  return false;
}
static void SaveCycleConfig() {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  FILE *f = fopen(CyclePath().c_str(), "w");
  if (!f) return;
  fprintf(f, "@secs=%d\n", g_cycle_secs.load());
  for (size_t i = 0; i < g_mode_names.size() && i < g_checked.size(); ++i)
    if (g_checked[i]) fprintf(f, "%s\n", g_mode_names[i].c_str());
  fclose(f);
}
static void LoadCycleConfig() {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  FILE *f = fopen(CyclePath().c_str(), "r");
  if (!f) { g_checked.assign(g_mode_names.size(), 1); return; }   // default: everything checked
  std::vector<char> m(g_mode_names.size(), 0);
  char line[256];
  while (fgets(line, sizeof line, f)) {
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    if (s.empty()) continue;
    if (s.compare(0, 6, "@secs=") == 0) {                     // persisted cycle time
      const int v = atoi(s.c_str() + 6);
      if (v >= 1 && v <= 3600) g_cycle_secs.store(v);
      continue;
    }
    for (size_t i = 0; i < g_mode_names.size(); ++i)
      if (g_mode_names[i] == s) { m[i] = 1; break; }
  }
  fclose(f);
  g_checked.swap(m);
}
// Next/first mode index that's in the cycle set (empty set => treat as all).
static int NextChecked(int cur, int nmodes) {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  const bool all = !AnyChecked();                 // empty set => treat as all
  for (int step = 1; step <= nmodes; ++step) {
    const int i = (cur + step) % nmodes;
    if (all || ((size_t)i < g_checked.size() && g_checked[i])) return i;
  }
  return cur;
}
static int FirstChecked(int nmodes) {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  const bool all = !AnyChecked();
  for (int i = 0; i < nmodes; ++i)
    if (all || ((size_t)i < g_checked.size() && g_checked[i])) return i;
  return 0;
}

// --------------------------------------------------------------------------
// Per-mode arguments (for arg-taking modes like Stock Ticker / Jellyfin),
// persisted to ~/led-matrix-server/args.conf as "<mode name>\t<arg>" lines so a
// cycled mode uses the user's chosen symbol/title instead of the generic
// DefaultArg.
// --------------------------------------------------------------------------
static std::string ArgsPath() {
  const char *h = getenv("HOME");
  return std::string(h ? h : "/root") + "/led-matrix-server/args.conf";
}
static void SaveArgs() {
  std::lock_guard<std::mutex> lk(g_args_mtx);
  FILE *f = fopen(ArgsPath().c_str(), "w");
  if (!f) return;
  for (const auto &kv : g_mode_args)
    if (!kv.second.empty()) fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
  fclose(f);
}
// The persisted arg for a mode name, or "" if none.
static std::string ArgFor(const std::string &name) {
  std::lock_guard<std::mutex> lk(g_args_mtx);
  auto it = g_mode_args.find(name);
  return it == g_mode_args.end() ? std::string() : it->second;
}
// Load persisted args and push them into the matching modes so cycling and
// startup honor them.
static void LoadArgs() {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  FILE *f = fopen(ArgsPath().c_str(), "r");
  if (f) {
    char line[512];
    while (fgets(line, sizeof line, f)) {
      std::string s(line);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
      const size_t tab = s.find('\t');
      if (tab == std::string::npos) continue;
      const std::string name = s.substr(0, tab), arg = s.substr(tab + 1);
      std::lock_guard<std::mutex> lk(g_args_mtx);
      g_mode_args[name] = arg;
    }
    fclose(f);
  }
  if (g_modes)                                  // apply to the live modes
    for (Mode *m : *g_modes) {
      const std::string a = ArgFor(m->name());
      if (!a.empty()) m->SetArg(a);
    }
}

// Replace the fetched-module modes (everything past the built-ins) with a fresh
// set built from what is installed on disk, and refresh the name list the
// servers render from. Render thread only, between frames -- never while a
// Draw() is in flight, since it deletes the outgoing module modes.
static void RebuildModuleModes(std::vector<Mode *> &modes) {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  for (size_t i = g_nbuiltin; i < modes.size(); ++i) delete modes[i];
  modes.resize(g_nbuiltin);
  const std::vector<Mode *> mods = modules::BuildModes();
  for (size_t i = 0; i < mods.size(); ++i) modes.push_back(mods[i]);
  g_mode_names.clear();
  for (size_t i = 0; i < modes.size(); ++i) g_mode_names.push_back(modes[i]->name());
  FitChecked(false);   // modes appearing mid-run start unchecked; the caller opts them in
}

// Apply a selection: mode number (1-based) + optional arg. val==0 starts
// cycling (arg = seconds). Shared by the web form and the JSON API. Returns a
// one-line human-readable status.
// argGiven distinguishes "the caller supplied an empty arg" (a cleared web
// textbox, which must be applied) from "the caller supplied no arg at all".
static std::string ApplySelection(int val, const std::string &arg, bool argGiven = false) {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  if (val == 0) {                             // mode 0: auto-cycle the checked modes
    int secs = arg.empty() ? 15 : atoi(arg.c_str());
    secs = secs < 1 ? 1 : (secs > 3600 ? 3600 : secs);
    g_cycle_secs.store(secs);
    if (g_modes)                              // use each mode's persisted arg, else its default
      for (Mode *m : *g_modes) {
        const std::string a = ArgFor(m->name());
        if (!a.empty()) m->SetArg(a);
        else if (const char *d = m->DefaultArg()) m->SetArg(d);
      }
    g_current_mode.store(FirstChecked((int)g_mode_names.size()));
    g_cycle.store(true);
    SaveCycleConfig();                        // persist the cycle time
    char ok[120];
    snprintf(ok, sizeof(ok), "Cycling checked modes, %ds each.", secs);
    return ok;
  }
  if (val < 1 || val > (int)g_mode_names.size()) return "Invalid selection.";
  g_cycle.store(false);                        // any explicit pick stops cycling
  g_current_mode.store(val - 1);
  if (g_modes) {
    Mode *m = (*g_modes)[val - 1];
    if (m->TakesArg() && (argGiven || !arg.empty())) {
      // Remember what was actually selected, so the web textbox, a later restart
      // and the cycle all use it. Without this the page re-rendered from the
      // last /args save and silently threw away the edit that was just applied.
      { std::lock_guard<std::mutex> lk(g_args_mtx); g_mode_args[m->name()] = arg; }
      SaveArgs();
      m->SetArg(arg);
    } else if (!arg.empty()) {
      m->SetArg(arg);
    }
  }
  char ok[200];
  snprintf(ok, sizeof(ok), "Switched to mode %d: %s%s%s", val,
           g_mode_names[val - 1].c_str(),
           arg.empty() ? "" : "  arg=", arg.c_str());
  return ok;
}

// --------------------------------------------------------------------------
// HTTP control server. Pick a mode (with optional arg) from a button grid, and
// paste a Lua script into a textbox that is persisted to disk. A JSON API at
// /api (GET to read state, POST to switch/cycle) mirrors it for scripting.
// Deliberately tiny and dependency-free so
// the binary stays a single static executable.
// --------------------------------------------------------------------------
// Percent-encode for a query string (used to carry a status note through the
// post/redirect/get that keeps reloads from re-posting).
static std::string UrlEncode(const std::string &s) {
  static const char *hex = "0123456789ABCDEF";
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = (unsigned char)s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

static std::string UrlDecode(const std::string &s) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  std::string out; out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) { out += (char)((hex(s[i+1]) << 4) | hex(s[i+2])); i += 2; }
    else out += s[i];
  }
  return out;
}

static std::string HtmlEscape(const std::string &s) {
  std::string o; o.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;  case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;   case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

// Pull one field out of an application/x-www-form-urlencoded body.
static std::string FormField(const std::string &body, const std::string &key) {
  const std::string k = key + "=";
  size_t p = 0;
  while (p <= body.size()) {
    const size_t amp = body.find('&', p);
    const std::string pair = body.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    if (pair.compare(0, k.size(), k) == 0) return UrlDecode(pair.substr(k.size()));
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  return "";
}

// Whether the form carried `key` at all -- an empty field is not the same as a
// missing one (clearing an arg textbox has to be applied, not ignored).
// Every value posted under `key` (checkboxes share one name).
static std::vector<std::string> FormFields(const std::string &body, const std::string &key) {
  std::vector<std::string> out;
  const std::string pat = key + "=";
  size_t p = 0;
  while (p < body.size()) {
    size_t amp = body.find('&', p);
    if (amp == std::string::npos) amp = body.size();
    const std::string kv = body.substr(p, amp - p);
    if (kv.compare(0, pat.size(), pat) == 0) out.push_back(UrlDecode(kv.substr(pat.size())));
    p = amp + 1;
  }
  return out;
}

static bool HasFormField(const std::string &body, const std::string &key) {
  const std::string k = key + "=";
  size_t p = 0;
  while (p <= body.size()) {
    const size_t amp = body.find('&', p);
    if (body.compare(p, k.size(), k) == 0) return true;
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  return false;
}

static std::string BuildWebPage(const std::string &banner, const std::string &luaError = "") {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  const int cur = g_current_mode.load();
  const bool cycling = g_cycle.load();
  const std::string script = g_lua_host ? g_lua_host->Current() : "";
  // Error to show under the textbox: an explicit one (e.g. a submit-time syntax
  // error) if given, else the live runtime error from the last setup()/loop().
  std::string luaErr = luaError;
  if (luaErr.empty() && g_lua_host) luaErr = g_lua_host->LastError();

  std::string h;
  h += "<!doctype html><html><head><meta charset=utf-8>";
  h += "<meta name=viewport content=\"width=device-width,initial-scale=1\">";
  h += "<title>LED Matrix Server</title><style>";
  h += "body{background:#0b0f14;color:#d7e0ea;font:15px/1.5 system-ui,-apple-system,sans-serif;"
       "margin:0;padding:24px}.wrap{max-width:920px;margin:auto}"
       "h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:28px 0 8px}"
       ".status{color:#8aa0b4;margin:0 0 16px}.banner{background:#123;border:1px solid #2a4;"
       "color:#bfe;padding:8px 12px;border-radius:6px;margin:0 0 16px}"
       ".err{background:#311;border-color:#a44;color:#fbb}"
       ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:8px;margin-top:10px}"
       ".cell{display:flex;align-items:center;gap:8px}.cell button{flex:1;min-width:0}"
       ".chk{flex:none;width:17px;height:17px;accent-color:#4a7;cursor:pointer}"
       "button{background:#18212b;color:#d7e0ea;border:1px solid #2a3a4a;border-radius:6px;"
       "padding:9px 10px;font:inherit;cursor:pointer;text-align:left}button:hover{border-color:#4a7}"
       "button.cur{background:#1c3a2a;border-color:#4a7;color:#cff}"
       "input,textarea{background:#0f1620;color:#d7e0ea;border:1px solid #2a3a4a;border-radius:6px;"
       "padding:8px;font:inherit;box-sizing:border-box}input{width:340px;max-width:100%}"
       "textarea{width:100%;font-family:ui-monospace,Menlo,monospace;font-size:13px}"
       ".apply{background:#1c3a2a;border-color:#4a7}"
       "code{background:#0f1620;padding:1px 5px;border-radius:4px}"
       ".help{color:#8aa0b4;font-size:13px}"
       ".btnrow{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap;align-items:center}"
       ".secslbl{display:flex;align-items:center;gap:6px;color:#b8c6d4}.secs{width:72px}"
       ".argsec{display:flex;flex-direction:column;gap:14px;margin-top:10px}"
       ".argrow{display:flex;align-items:center;gap:8px}"
       ".arguse{color:#8aa0b4;font-size:13px;margin:4px 0 0 25px}"
       ".argform{display:flex;align-items:center;gap:8px;flex:1;flex-wrap:wrap}"
       ".argform button{min-width:150px}.argbox{flex:1;min-width:160px;width:auto}"
       ".errbox{background:#311;border:1px solid #a44;color:#fbb;padding:8px 12px;"
       "border-radius:6px;margin-top:10px;font-family:ui-monospace,Menlo,monospace;"
       "font-size:13px;white-space:pre-wrap;word-break:break-word}"
       "dialog{background:#0f1620;color:#d7e0ea;border:1px solid #2a3a4a;border-radius:10px;"
       "padding:0;width:min(880px,94vw);max-height:88vh}"
       "dialog::backdrop{background:rgba(0,0,0,.6)}"
       ".dlghd{display:flex;align-items:center;gap:10px;padding:14px 18px;"
       "border-bottom:1px solid #2a3a4a;position:sticky;top:0;background:#0f1620}"
       ".dlghd h2{margin:0;flex:1}.dlgbody{padding:14px 18px;overflow:auto;max-height:64vh}"
       ".dlgft{display:flex;gap:8px;padding:12px 18px;border-top:1px solid #2a3a4a;"
       "position:sticky;bottom:0;background:#0f1620}"
       ".dlgft button{min-width:110px;text-align:center}"
       ".dlgbody .btnrow{margin-bottom:14px}"
       "button.pen{flex:none;width:30px;padding:9px 0;text-align:center;line-height:1;"
       "font-size:15px;color:#7f93a8}"
       "button.pen:hover{color:#cff;border-color:#4a7}"
       ".cat{border:1px solid #223040;border-radius:8px;padding:10px 12px;margin-bottom:14px}"
       ".cathd{display:flex;align-items:center;gap:8px;margin-bottom:8px}"
       ".cathd .u{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;"
       "color:#8aa0b4;font-size:13px}"
       ".tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}"
       ".tile{display:block;position:relative;border:1px solid #2a3a4a;border-radius:8px;"
       "overflow:hidden;cursor:pointer;background:#131c26}"
       ".tile:hover{border-color:#4a7}"
       // Same native checkbox as the main page (accent-color), sitting top-left.
       ".tile input{position:absolute;top:6px;left:6px;width:17px;height:17px;"
       "accent-color:#4a7;cursor:pointer;z-index:1;margin:0}"
       ".tile .shot{width:100%;aspect-ratio:2/1;object-fit:cover;display:block;background:#0b0f14}"
       ".tile .noshot{width:100%;aspect-ratio:2/1;display:flex;align-items:center;"
       "justify-content:center;color:#54637a;font-size:12px;background:#0b0f14}"
       ".tile .meta{padding:7px 9px}.tile .nm{font-size:13px}"
       ".tile .ds{color:#8aa0b4;font-size:12px;margin-top:2px}"
       ".tile input:checked~.meta{background:#16281f}"
       ".tile input:checked~.shot,.tile input:checked~.noshot{opacity:1}"
       ".tile .shot,.tile .noshot{opacity:.55}"
       "table.api{border-collapse:collapse;font-size:13px;margin:10px 0}"
       ".api td{padding:3px 14px 3px 0;vertical-align:top;color:#b8c6d4}"
       ".api td:first-child{white-space:nowrap}"
       "</style></head><body><div class=wrap>";

  h += "<h1>LED Matrix Server</h1>";
  if (!banner.empty()) {
    const bool bad = banner.compare(0, 5, "ERROR") == 0 || banner == "Invalid selection.";
    h += std::string("<div class=\"banner") + (bad ? " err" : "") + "\">" + HtmlEscape(banner) + "</div>";
  }

  // Mode picker: one form with a shared arg field. Clicking a mode's button
  // switches to it (name=mode value=N); "Cycle checked" (value 0) cycles the
  // checked set at the cycle-time interval. Each mode's checkbox toggles its
  // membership and the cycle-time box persist immediately (saveChecks/saveSecs)
  // so both survive a reload/restart without a full form submit. While cycling,
  // only "Cycle checked" is highlighted -- no individual mode.
  int luaIdx = -1;                                  // Lua mode -> its own selector below
  if (g_modes && g_lua_host)
    for (size_t i = 0; i < g_modes->size(); ++i)
      if ((*g_modes)[i] == g_lua_host->AsMode()) { luaIdx = (int)i; break; }
  char secsbuf[16]; snprintf(secsbuf, sizeof secsbuf, "%d", g_cycle_secs.load());
  h += "<form method=post action=/mode>";
  h += "<div class=btnrow>";
  h += "<button class=cycle name=mode value=0>&#x21bb; Cycle checked</button>";
  h += std::string("<label class=secslbl>Cycle time (s): <input class=secs name=secs "
       "type=number min=1 max=3600 onchange=saveSecs() value=") + secsbuf + "></label>";
  h += "<button type=button onclick=toggleAll()>Check / uncheck all</button>";
  h += "</div>";
  h += "<div class=grid>";
  for (size_t i = 0; i < g_nbuiltin && i < g_mode_names.size(); ++i) {
    if (g_modes && (*g_modes)[i]->TakesArg()) continue;   // arg modes get their own section
    if ((int)i == luaIdx) continue;                       // Lua mode selector is below
    char v[16]; snprintf(v, sizeof v, "%zu", i + 1);
    const bool on = i < g_checked.size() && g_checked[i] != 0;
    const bool hi = (int)i == cur;                  // current mode always highlighted
    h += "<div class=cell>";
    h += std::string("<input type=checkbox class=chk onchange=saveChecks() data-mode=") +
         std::to_string(i) + (on ? " checked" : "") + ">";
    h += std::string("<button name=mode value=") + v + (hi ? " class=cur" : "") + ">"
         + HtmlEscape(g_mode_names[i]) + "</button>";
    h += "</div>";
  }
  h += "</div></form>";

  // Modes with arguments: their own section, each with a persisted arg textbox
  // and its own mini-form so its checkbox still feeds the shared cycle set.
  if (g_modes) {
    std::string sec;
    for (size_t i = 0; i < g_nbuiltin && i < g_modes->size(); ++i) {   // built-ins only
      Mode *m = (*g_modes)[i];
      if (!m->TakesArg()) continue;
      char v[16]; snprintf(v, sizeof v, "%zu", i + 1);
      const bool on = i < g_checked.size() && g_checked[i] != 0;
      const bool hi = (int)i == cur;
      const std::string curArg = ArgFor(m->name());
      sec += "<div><div class=argrow>";
      sec += std::string("<input type=checkbox class=chk onchange=saveChecks() data-mode=") +
             std::to_string(i) + (on ? " checked" : "") + ">";
      sec += "<form method=post action=/mode class=argform>";
      sec += std::string("<input type=hidden name=mode value=") + v + ">";
      sec += std::string("<input class=argbox name=arg value=\"") + HtmlEscape(curArg) +
             "\" placeholder=\"" + HtmlEscape(m->ArgHint()) + "\" onchange=\"saveArg(" +
             std::to_string(i) + ",this.value)\">";
      sec += std::string("<button type=submit") + (hi ? " class=cur" : "") + ">"
             + HtmlEscape(m->name()) + "</button>";
      sec += "</form></div>";
      // Usage under the row, indented past the checkbox to line up with the box.
      if (*m->ArgUsage())
        sec += "<div class=arguse>" + HtmlEscape(m->ArgUsage()) + "</div>";
      sec += "</div>";
    }
    if (!sec.empty()) {
      h += "<h2>Modes with arguments</h2>";
      h += "<p class=help>These modes take an argument (saved as you type). It's used when the "
           "mode is selected or cycled &mdash; so a checked arg-mode cycles with your value.</p>";
      h += "<div class=argsec>" + sec + "</div>";
    }
  }
  // Fetched modules. Plain ones go in the button grid; ones whose script
  // declares an `arghint` get their own row with an argument textbox (like the
  // built-in arg modes). Both carry the cycle checkbox and the edit pencil.
  {
    std::string grid, argsec;
    for (size_t i = g_nbuiltin; i < g_mode_names.size(); ++i) {
      char v[16]; snprintf(v, sizeof v, "%zu", i + 1);
      const bool on = i < g_checked.size() && g_checked[i] != 0;
      const bool hi = (int)i == cur;
      Mode *m = g_modes ? (*g_modes)[i] : nullptr;
      const std::string chk =
          std::string("<input type=checkbox class=chk onchange=saveChecks() data-mode=") +
          std::to_string(i) + (on ? " checked" : "") + ">";
      const std::string pen =
          std::string("<button type=button class=pen title=\"Edit this script below\" "
          "onclick=\"editScript(") + v + ")\">&#x270e;</button>";
      const std::string st = m ? m->StatusLine() : std::string();
      const std::string errbox = st.empty() ? "" :
          "<div class=errbox>" + HtmlEscape(g_mode_names[i] + ": " + st) + "</div>";
      if (m && m->TakesArg()) {
        const std::string curArg = ArgFor(m->name());
        argsec += "<div><div class=argrow>" + chk;
        argsec += "<form method=post action=/mode class=argform>";
        argsec += std::string("<input type=hidden name=mode value=") + v + ">";
        argsec += std::string("<button type=submit") + (hi ? " class=cur" : "") + ">" +
                  HtmlEscape(g_mode_names[i]) + "</button>";
        argsec += std::string("<input class=argbox name=arg value=\"") + HtmlEscape(curArg) +
                  "\" placeholder=\"" + HtmlEscape(m->ArgHint()) + "\" onchange=\"saveArg(" +
                  std::to_string(i) + ",this.value)\">";
        argsec += pen + "</form></div>" + errbox + "</div>";
      } else {
        grid += "<div class=cell>" + chk +
                std::string("<button name=mode value=") + v + (hi ? " class=cur" : "") + ">" +
                HtmlEscape(g_mode_names[i]) + "</button>" + pen + "</div>" + errbox;
      }
    }
    h += "<h2>LUA Modules</h2>";
    h += "<div class=btnrow><button type=button "
         "onclick=\"document.getElementById('mdlg').showModal()\">"
         "&#x2b07; Fetch LUA modules from github</button></div>";
    if (grid.empty() && argsec.empty())
      h += "<p class=help>None installed.</p>";
    if (!grid.empty())
      h += "<form method=post action=/mode><div class=grid>" + grid + "</div></form>";
    if (!argsec.empty())
      h += "<div class=argsec>" + argsec + "</div>";
  }

  // The module fetch dialog: one screenshot tile per module in the project's
  // GitHub catalog, each checkbox pre-set to whether it is installed. Apply
  // posts the whole checked set, so ticking installs and unticking removes.
  // The catalog is fixed to the GitHub repo -- there is no chooser.
  {
    const std::vector<modules::CatalogInfo> cats = modules::Catalogs();
    h += "<dialog id=mdlg><form method=post action=/modules>";
    h += "<div class=dlghd><h2>LUA Modules</h2>"
         "<button type=button onclick=\"document.getElementById('mdlg').close()\">Close</button>"
         "</div><div class=dlgbody>";
    h += "<p class=help>Tick the modules you want and press Apply &mdash; ticked modules are "
         "downloaded from the project's GitHub repo and appear under <b>LUA Modules</b>, "
         "unticked ones are removed.</p>";
    h += "<div class=btnrow>"
         "<button type=button onclick=toggleMods()>Select all / none</button>"
         "<button name=act value=refresh>Refresh list</button></div>";
    for (size_t c = 0; c < cats.size(); ++c) {
      const modules::CatalogInfo &cat = cats[c];
      if (!cat.error.empty()) {
        h += "<div class=errbox>" + HtmlEscape(cat.error) + "</div>";
        continue;
      }
      h += "<div class=tiles>";
      for (size_t m = 0; m < cat.modules.size(); ++m) {
        const modules::ModuleInfo &mi = cat.modules[m];
        h += "<label class=tile>";
        h += std::string("<input type=checkbox name=m value=\"") +
             HtmlEscape(modules::Key(cat.url, mi.id)) + "\"" +
             (mi.installed ? " checked" : "") + ">";
        if (mi.shot_url.empty())
          h += "<div class=noshot>no screenshot</div>";
        else
          h += "<img class=shot loading=lazy alt=\"\" src=\"" + HtmlEscape(mi.shot_url) + "\">";
        h += "<div class=meta><div class=nm>" + HtmlEscape(mi.name) + "</div>";
        if (!mi.desc.empty()) h += "<div class=ds>" + HtmlEscape(mi.desc) + "</div>";
        h += "</div></label>";
      }
      h += "</div>";
    }
    h += "</div><div class=dlgft>";
    h += "<button class=apply name=act value=apply>Apply</button>";
    h += "<button type=button onclick=\"document.getElementById('mdlg').close()\">Cancel</button>";
    h += "</div></form></dialog>";
  }

  // Keep the button highlight in sync with the panel: poll the state and mark
  // the current mode's button (and the cycle button only while cycling). This
  // makes the highlight follow the auto-cycler live, and drop off "Cycle
  // checked" the moment a specific mode is picked.
  // Highlight = which mode is on the panel (follows the auto-cycler live). The
  // "Cycle checked" button (value 0) is never highlighted -- the highlight only
  // ever marks the active mode.
  h += "<script>function reflect(){"
       "fetch('/api').then(function(r){return r.json();}).then(function(d){"
       "document.querySelectorAll('button[name=mode]').forEach(function(b){"
       "b.classList.toggle('cur', b.value!=='0' && parseInt(b.value,10)===d.current);"
       "});}).catch(function(){});}"
       "setInterval(reflect,2000);reflect();"
       // Pen button: pull an installed module's Lua into the editor below.
       "function editScript(n){fetch('/modules/script?n='+n)"
       ".then(function(r){return r.text();}).then(function(t){"
       "var ta=document.getElementById('lua-script');if(!ta)return;"
       "ta.value=t;ta.scrollIntoView({behavior:'smooth',block:'center'});ta.focus();"
       "}).catch(function(){});}"
       "function saveChecks(){var b=[];"
       "document.querySelectorAll('.chk').forEach(function(c){if(c.checked)b.push(c.dataset.mode);});"
       "fetch('/checks',{method:'POST',"
       "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
       "body:'checked='+b.join(',')});}"
       "function toggleMods(){var cs=document.querySelectorAll('#mdlg input[name=m]'),all=true;"
       "cs.forEach(function(c){if(!c.checked)all=false;});"
       "cs.forEach(function(c){c.checked=!all;});}"
       "function toggleAll(){var cs=document.querySelectorAll('.chk'),all=true;"
       "cs.forEach(function(c){if(!c.checked)all=false;});"
       "cs.forEach(function(c){c.checked=!all;});saveChecks();}"
       "if(location.search.indexOf('modules=1')>=0)"
       "document.getElementById('mdlg').showModal();"
       "function saveSecs(){var s=document.querySelector('[name=secs]').value;"
       "fetch('/cyclesecs',{method:'POST',"
       "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
       "body:'secs='+encodeURIComponent(s)});}"
       // keepalive: pressing Enter in an arg box fires change *and* submits the
       // form, and the navigation would otherwise abort this save in flight.
       "function saveArg(i,v){fetch('/args',{method:'POST',keepalive:true,"
       "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
       "body:'mode='+i+'&arg='+encodeURIComponent(v)});}</script>";

  h += "<h2>Custom LUA script</h2>";
  // The Lua mode's own selector (moved out of the main grid) sits right under
  // the heading, with its checkbox so it can still be added to the cycle.
  if (luaIdx >= 0) {
    char v[16]; snprintf(v, sizeof v, "%d", luaIdx + 1);
    const bool on = luaIdx >= 0 && (size_t)luaIdx < g_checked.size() && g_checked[luaIdx] != 0;
    const bool hi = !cycling && luaIdx == cur;
    h += "<div class=argrow>";
    h += std::string("<input type=checkbox class=chk onchange=saveChecks() data-mode=") +
         std::to_string(luaIdx) + (on ? " checked" : "") + ">";
    h += "<form method=post action=/mode class=argform>";
    h += std::string("<button name=mode value=") + v + (hi ? " class=cur" : "") + ">Custom LUA script</button>";
    h += "</form></div>";
  }
  h += "<p class=help>Define <code>setup()</code>, called once, and <code>loop(t, frame)</code>, "
       "called every frame (t = seconds elapsed, frame = frame counter). Globals set in "
       "<code>setup()</code> persist into <code>loop()</code> by normal Lua scoping. Colors are "
       "0&ndash;255; coordinates are 0-based from the top-left. Sandboxed to "
       "base/table/string/math. Saved to disk and reloaded on restart; applying jumps to the "
       "Lua mode.</p>";

  // Graphics API reference.
  const struct { const char *sig; const char *desc; } kApi[] = {
    {"width()",                  "canvas width in pixels"},
    {"height()",                 "canvas height in pixels"},
    {"clear([r,g,b])",           "fill the whole buffer (default black)"},
    {"color(r,g,b)",             "set the pen; later calls may omit r,g,b to use it"},
    {"setpixel(x,y[,r,g,b])",    "set a single pixel"},
    {"line(x0,y0,x1,y1[,r,g,b])","draw a line between two points"},
    {"rect(x,y,w,h[,r,g,b])",    "filled rectangle, top-left at (x,y)"},
    {"circle(x,y,rad[,r,g,b])",  "filled disc of radius rad, centered at (x,y)"},
    {"triangle(x0,y0,x1,y1,x2,y2[,r,g,b])", "triangle outline through 3 points"},
    {"filltriangle(x0,y0,x1,y1,x2,y2[,r,g,b])", "filled triangle"},
    {"text(x,y,str[,r,g,b])",    "draw text (6x10 font, y = top edge)"},
    {"scrollh(n)",               "wrap-scroll the whole buffer left/right by n pixels"},
    {"scrollv(n)",               "wrap-scroll the whole buffer up/down by n pixels"},
  };
  h += "<table class=api>";
  for (const auto &f : kApi)
    h += std::string("<tr><td><code>") + f.sig + "</code></td><td>" + f.desc + "</td></tr>";
  h += "</table>";

  h += "<form method=post action=/lua>";
  h += "<textarea id=lua-script name=script rows=18 spellcheck=false>" + HtmlEscape(script) + "</textarea>";
  h += "<div class=btnrow>";
  h += "<button class=apply type=submit>Apply &amp; Save</button>";
  h += "<button type=button onclick=\"insertExample()\">Insert example</button>";
  h += "</div></form>";

  // Error (syntax on submit, or runtime from setup()/loop()) shown below the box.
  if (!luaErr.empty())
    h += "<div class=errbox><b>Lua error:</b> " + HtmlEscape(luaErr) + "</div>";

  // Sample script (raw text inside a non-executed script tag, so no escaping),
  // and a helper that appends it to the end of the textbox. This is the same
  // program the app ships as its default, so it always matches the live API.
  h += "<script id=lua-example type=\"text/plain\">";
  h += LuaHost::DefaultScript();
  h += "</script>";
  h += "<script>function insertExample(){"
       "var t=document.getElementById('lua-script'),"
       "e=document.getElementById('lua-example').textContent;"
       "if(t.value.length&&t.value.slice(-1)!=='\\n')t.value+='\\n';"
       "t.value+=e;t.focus();t.scrollTop=t.scrollHeight;}</script>";

  h += "</div></body></html>";
  return h;
}

// A client connection: a plain socket, or (when built with TLS and a cert is
// loaded) an OpenSSL connection over it. The HTTP handler talks only to this,
// so it is oblivious to whether the transport is encrypted.
struct Conn {
  int fd = -1;
#ifdef USE_TLS
  SSL *ssl = nullptr;
#endif
  // >0 bytes read, 0 on clean close, <0 on error (matches recv()).
  ssize_t read(void *buf, size_t n) {
#ifdef USE_TLS
    if (ssl) { const int r = SSL_read(ssl, buf, (int)n); return r > 0 ? r : (r == 0 ? 0 : -1); }
#endif
    return recv(fd, buf, n, 0);
  }
  void write(const void *buf, size_t n) {
#ifdef USE_TLS
    if (ssl) { SSL_write(ssl, buf, (int)n); return; }
#endif
    send(fd, buf, n, MSG_NOSIGNAL);
  }
  void close_() {
#ifdef USE_TLS
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
#endif
    if (fd >= 0) { ::close(fd); fd = -1; }
  }
};

static void SendHttp(Conn &c, const char *status, const char *ctype, const std::string &body) {
  char hdr[256];
  const int n = snprintf(hdr, sizeof hdr,
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
      "Connection: close\r\n\r\n", status, ctype, body.size());
  c.write(hdr, n);
  c.write(body.data(), body.size());
}

// --- Optional HTTP basic auth (-A user:pass) ------------------------------
static std::string Base64(const std::string &in) {
  static const char *T =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  int val = 0, bits = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c; bits += 8;
    while (bits >= 0) { o += T[(val >> bits) & 0x3F]; bits -= 6; }
  }
  if (bits > -6) o += T[((val << 8) >> (bits + 8)) & 0x3F];
  while (o.size() % 4) o += '=';
  return o;
}
// Value of a header (found case-insensitively) from the raw header block, with
// surrounding whitespace trimmed. The value keeps its original case (base64 is
// case-sensitive). Empty if absent.
static std::string HeaderValue(const std::string &head, const char *lname) {
  std::string lo = head;
  for (char &c : lo) c = (char)tolower((unsigned char)c);
  const std::string key = std::string("\n") + lname + ":";   // headers follow the request line
  const size_t p = lo.find(key);
  if (p == std::string::npos) return "";
  const size_t s = p + key.size();
  const size_t e = head.find('\n', s);
  std::string v = head.substr(s, e == std::string::npos ? std::string::npos : e - s);
  const size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : v.substr(a, b - a + 1);
}
// True if basic auth is off, or the request presents the right credentials.
static bool AuthOk(const std::string &headers) {
  if (g_web_auth.empty()) return true;
  std::string a = HeaderValue(headers, "authorization");
  if (a.size() < 6) return false;
  std::string scheme = a.substr(0, 5);
  for (char &c : scheme) c = (char)tolower((unsigned char)c);
  if (scheme != "basic") return false;
  const size_t t = a.find_first_not_of(' ', 5);            // token after "Basic"
  return t != std::string::npos && a.substr(t) == Base64(g_web_auth);
}
static void SendUnauthorized(Conn &c) {
  static const char *body = "Unauthorized\n";
  char hdr[256];
  const int n = snprintf(hdr, sizeof hdr,
      "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"led-matrix-server\"\r\n"
      "Content-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
      strlen(body));
  c.write(hdr, n);
  c.write(body, strlen(body));
}

// --- JSON API (/api) ------------------------------------------------------
static std::string JsonEscape(const std::string &s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

// A JSON snapshot of the mode list and current state, shared by GET /api and
// the reply to a successful POST /api.
static std::string StateJson() {
  std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
  const int cur = g_current_mode.load();
  std::string j = "{";
  j += "\"current\":" + std::to_string(cur + 1);
  j += ",\"current_name\":\"" +
       JsonEscape(cur >= 0 && cur < (int)g_mode_names.size() ? g_mode_names[cur] : "") + "\"";
  j += ",\"cycling\":" + std::string(g_cycle.load() ? "true" : "false");
  j += ",\"cycle_secs\":" + std::to_string(g_cycle_secs.load());
  j += ",\"builtin_count\":" + std::to_string((int)g_nbuiltin);
  j += ",\"modes\":[";
  for (size_t i = 0; i < g_mode_names.size(); ++i) {
    Mode *m = g_modes ? (*g_modes)[i] : nullptr;
    const bool on = i < g_checked.size() && g_checked[i] != 0;
    if (i) j += ",";
    j += "{\"n\":" + std::to_string(i + 1);
    j += ",\"name\":\"" + JsonEscape(g_mode_names[i]) + "\"";
    j += ",\"builtin\":" + std::string(i < g_nbuiltin ? "true" : "false");
    j += ",\"checked\":" + std::string(on ? "true" : "false");
    j += ",\"takes_arg\":" + std::string(m && m->TakesArg() ? "true" : "false");
    if (m && m->TakesArg()) j += ",\"arg\":\"" + JsonEscape(ArgFor(m->name())) + "\"";
    j += "}";
  }
  j += "]}";
  return j;
}

// POST /api body: {"mode":N[,"arg":"..."]} switches (N is 1-based; N=0 cycles,
// with optional {"secs":N}). Returns the new state, or {"ok":false,"error":...}.
static std::string ApiPost(const std::string &body) {
  Json root;
  std::string err;
  if (!Json::Parse(body, &root, &err) || root.type != Json::OBJ)
    return "{\"ok\":false,\"error\":\"" + JsonEscape("invalid JSON: " + err) + "\"}";
  const Json *jm = root.Get("mode");
  if (!jm || jm->type != Json::NUM)
    return "{\"ok\":false,\"error\":\"missing numeric \\\"mode\\\"\"}";
  const int val = (int)jm->num;
  const Json *ja = root.Get("arg");
  const Json *js = root.Get("secs");
  std::string arg;
  bool argGiven = false;
  if (val == 0 && js && js->type == Json::NUM) {           // cycle: secs is the arg
    arg = std::to_string((int)js->num);
  } else if (ja) {
    argGiven = true;
    arg = (ja->type == Json::STR) ? ja->str
        : (ja->type == Json::NUM) ? std::to_string((long long)ja->num) : std::string();
  }
  const std::string msg = ApplySelection(val, arg, argGiven);
  if (msg == "Invalid selection.")
    return "{\"ok\":false,\"error\":\"invalid mode number\"}";
  std::string j = StateJson();
  j.insert(1, "\"ok\":true,\"message\":\"" + JsonEscape(msg) + "\",");   // after the '{'
  return j;
}

// Submit a Lua script: syntax-check + persist + queue it, and on success switch
// to the Lua mode so it shows. Returns "" when the script is running cleanly
// (after a frame runs so setup()/loop() errors surface), else the error text
// (syntax error, or the first-frame runtime error). Shared by the /lua web form
// and the /api/lua raw-upload endpoint.
static std::string SubmitLua(const std::string &script) {
  if (!g_lua_host) return "Lua mode unavailable";
  std::string res = g_lua_host->Submit(script);
  if (!res.empty() && res.back() == '\n') res.pop_back();
  if (res != "OK") return res.compare(0, 7, "ERROR: ") == 0 ? res.substr(7) : res;
  {                                                  // jump to the Lua mode to show it
    std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
    if (g_modes)
      for (size_t i = 0; i < g_modes->size(); ++i)
        if ((*g_modes)[i] == g_lua_host->AsMode()) {
          g_cycle.store(false); g_current_mode.store((int)i); break;
        }
  }
  usleep(150000);                                    // let a frame run so errors surface
  return g_lua_host->LastError();                    // "" if clean
}

static void HandleHttpClient(Conn &c) {
  timeval tv; tv.tv_sec = 5; tv.tv_usec = 0;
  setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

  std::string req; char b[4096]; ssize_t n; size_t hdr_end;
  while ((hdr_end = req.find("\r\n\r\n")) == std::string::npos) {   // read headers
    n = c.read(b, sizeof b);
    if (n <= 0) { return; }
    req.append(b, n);
    if (req.size() > (1u << 20)) { return; }
  }

  const size_t sp1 = req.find(' ');
  const size_t sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) { return; }
  const std::string method = req.substr(0, sp1);
  const std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);

  // Optional HTTP basic auth, checked before the body is read so an
  // unauthenticated client can't make us buffer a large upload.
  if (!AuthOk(req.substr(0, hdr_end))) { SendUnauthorized(c); return; }

  // Read the rest of the body (POST) up to Content-Length.
  size_t clen = 0;
  { std::string lo = req.substr(0, hdr_end);
    for (char &c : lo) c = (char)tolower((unsigned char)c);
    const size_t cl = lo.find("content-length:");
    if (cl != std::string::npos) clen = (size_t)atol(req.c_str() + cl + 15); }
  std::string body = req.substr(hdr_end + 4);
  while (body.size() < clen) {
    n = c.read(b, sizeof b);
    if (n <= 0) break;
    body.append(b, n);
    if (body.size() > (2u << 20)) break;
  }

  if (method == "POST" && path == "/api") {
    SendHttp(c, "200 OK", "application/json", ApiPost(body));
  } else if (method == "GET" && path == "/api") {
    SendHttp(c, "200 OK", "application/json", StateJson());
  } else if (method == "GET" && path.compare(0, 15, "/modules/script") == 0) {
    // The Lua source of an installed module (1-based mode number), for the pen
    // button that loads it into the editor.
    const size_t np = path.find("n=");
    const int n = np == std::string::npos ? 0 : atoi(path.c_str() + np + 2);
    std::string src;
    { std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
      if (g_modes && n >= 1 && n <= (int)g_modes->size()) src = (*g_modes)[n - 1]->SourceText(); }
    SendHttp(c, "200 OK", "text/plain; charset=utf-8", src);
  } else if (method == "POST" && path == "/mode") {
    const int val = atoi(FormField(body, "mode").c_str());
    // Cycling uses the dedicated cycle-time field; a mode pick uses the arg field.
    const std::string arg = (val == 0) ? FormField(body, "secs") : FormField(body, "arg");
    ApplySelection(val, arg, val != 0 && HasFormField(body, "arg"));
    // Post/redirect/get with no status banner -- the highlighted button already
    // shows which mode is active.
    const std::string hdr = "HTTP/1.1 303 See Other\r\nLocation: /\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
    c.write(hdr.data(), hdr.size());
  } else if (method == "POST" && path == "/api/lua") {
    // Raw upload: the whole request body is the Lua script (this replaced the
    // old TCP upload port). curl --data-binary @s.lua http://<pi>:8080/api/lua
    const std::string err = SubmitLua(body);
    const std::string j = err.empty()
        ? std::string("{\"ok\":true}")
        : "{\"ok\":false,\"error\":\"" + JsonEscape(err) + "\"}";
    SendHttp(c, "200 OK", "application/json", j);
  } else if (method == "POST" && path == "/lua") {
    const std::string err = SubmitLua(FormField(body, "script"));   // web textarea
    SendHttp(c, "200 OK", "text/html; charset=utf-8",
             BuildWebPage(err.empty() ? "Script saved and applied." : "", err));
  } else if (method == "POST" && path == "/modules") {
    // The catalog is fixed to GitHub; the dialog only refreshes or applies.
    const std::string act = FormField(body, "act");
    std::string err;
    bool keepOpen = false;
    if (act == "refresh") {
      err = modules::RefreshCatalogs();
      keepOpen = true;                              // stay in the dialog to review
    } else if (act == "apply") {
      const std::string r = modules::ApplySelection(FormFields(body, "m"));
      if (!r.empty() && r[0] != '\1') err = r;      // '\1' marks success, not an error
      // ApplySelection only flags the mode list dirty; the render thread rebuilds
      // it on the next frame (~16ms). Wait a beat so the redirected page shows the
      // new set immediately instead of needing a manual refresh.
      usleep(200000);
    }
    // Apply closes the dialog and returns to the refreshed page; Refresh reopens
    // it. Only errors carry a banner; success is silent (the section updates).
    std::string loc = keepOpen ? "/?modules=1" : "/";
    if (!err.empty()) loc += (keepOpen ? "&note=" : "?note=") + UrlEncode("ERROR: " + err);
    const std::string hdr = std::string("HTTP/1.1 303 See Other\r\nLocation: ") + loc +
                            "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    c.write(hdr.data(), hdr.size());
  } else if (method == "POST" && path == "/checks") {
    // Rebuild the cycle set from a comma-separated list of checked mode indices.
    const std::string list = FormField(body, "checked");
    std::vector<char> m;
    { std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx); m.assign(g_mode_names.size(), 0); }
    for (size_t p = 0; p < list.size(); ) {
      const size_t c = list.find(',', p);
      const std::string tok = list.substr(p, c == std::string::npos ? std::string::npos : c - p);
      if (!tok.empty()) {
        const int idx = atoi(tok.c_str());
        if (idx >= 0 && (size_t)idx < m.size()) m[idx] = 1;
      }
      if (c == std::string::npos) break;
      p = c + 1;
    }
    { std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx); g_checked.swap(m); }
    SaveCycleConfig();
    SendHttp(c, "204 No Content", "text/plain", "");
  } else if (method == "POST" && path == "/cyclesecs") {
    int s = atoi(FormField(body, "secs").c_str());
    s = s < 1 ? 1 : (s > 3600 ? 3600 : s);
    g_cycle_secs.store(s);
    SaveCycleConfig();
    SendHttp(c, "204 No Content", "text/plain", "");
  } else if (method == "POST" && path == "/args") {
    // Persist and apply an arg-taking mode's argument.
    const int idx = atoi(FormField(body, "mode").c_str());
    const std::string arg = FormField(body, "arg");
    if (g_modes && idx >= 0 && idx < (int)g_modes->size() && (*g_modes)[idx]->TakesArg()) {
      { std::lock_guard<std::mutex> lk(g_args_mtx); g_mode_args[(*g_modes)[idx]->name()] = arg; }
      if (!arg.empty()) (*g_modes)[idx]->SetArg(arg);
      SaveArgs();
    }
    SendHttp(c, "204 No Content", "text/plain", "");
  } else if (method == "GET" && (path == "/" || path.compare(0, 2, "/?") == 0)) {
    std::string note;                       // status carried over from a redirect
    const size_t q = path.find("note=");
    if (q != std::string::npos) {
      const size_t e = path.find('&', q);
      note = UrlDecode(path.substr(q + 5, e == std::string::npos ? std::string::npos : e - q - 5));
    }
    SendHttp(c, "200 OK", "text/html; charset=utf-8", BuildWebPage(note));
  } else {
    SendHttp(c, "404 Not Found", "text/plain", "Not found\n");
  }
}

// One worker thread per connection: wrap the socket (doing the TLS handshake
// here, off the accept loop), serve one request, then tear it down.
static void ServeConn(int fd) {
  Conn c;
  c.fd = fd;
#ifdef USE_TLS
  if (g_ssl_ctx) {
    c.ssl = SSL_new(g_ssl_ctx);
    SSL_set_fd(c.ssl, fd);
    if (SSL_accept(c.ssl) <= 0) { c.close_(); return; }   // handshake failed / not TLS
  }
#endif
  HandleHttpClient(c);
  c.close_();
}

static void WebServerThread(int port) {
  const int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) { perror("socket"); return; }
  int one = 1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr; memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(sfd, (sockaddr *)&addr, sizeof addr) < 0) {
    fprintf(stderr, "Web server disabled (port %d unavailable).\n", port);
    close(sfd); return;
  }
  if (listen(sfd, 8) < 0) { perror("listen"); close(sfd); return; }
  const bool tls =
#ifdef USE_TLS
      g_ssl_ctx != nullptr;
#else
      false;
#endif
  fprintf(stderr, "Web control server on %s://<host>:%d/%s\n", tls ? "https" : "http", port,
          g_web_auth.empty() ? "" : "  (basic auth required)");
  while (g_running.load()) {
    const int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) continue;
    std::thread(ServeConn, cfd).detach();
  }
  close(sfd);
}

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
// Configuration. There are no command-line arguments: everything is read from
// led-matrix-server.conf (KEY=VALUE lines) sitting next to the binary.
// --------------------------------------------------------------------------
static std::string ExeDir() {
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
  std::string p(buf);
  const size_t s = p.find_last_of('/');
  return s == std::string::npos ? "." : p.substr(0, s);
}
static std::map<std::string, std::string> ReadConf(const std::string &path) {
  std::map<std::string, std::string> m;
  FILE *f = fopen(path.c_str(), "r");
  if (!f) return m;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
    char *eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    std::string key(p), val(eq + 1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    m[key] = val;
  }
  fclose(f);
  return m;
}
static int ConfInt(const std::map<std::string, std::string> &m, const char *k, int def) {
  auto it = m.find(k);
  return it == m.end() ? def : atoi(it->second.c_str());
}
static std::string ConfStr(const std::map<std::string, std::string> &m, const char *k) {
  auto it = m.find(k);
  return it == m.end() ? std::string() : it->second;
}

// --------------------------------------------------------------------------
// Per-panel calibration. A pass-through Canvas that scales each pixel's R/G/B
// by the gain of the 64x64 panel it lands in, then forwards to the real
// framebuffer. Gains come from panels.cal (written by the panel-cal tool).
// Zero cost when no panel is trimmed (falls back to the framebuffer directly).
// --------------------------------------------------------------------------
class CalibratedCanvas : public Canvas {
public:
  void SetTarget(FrameCanvas *t) { target_ = t; }
  // grid = chain(cols) x parallel(rows) of panelW x panelH panels.
  void Configure(int panelW, int panelH, int gcols, int grows, const std::string &calpath) {
    panelW_ = panelW; panelH_ = panelH; gcols_ = gcols; grows_ = grows;
    const int n = gcols * grows;
    gr_.assign(n, 256); gg_.assign(n, 256); gb_.assign(n, 256);   // 256 == 1.0 (8.8 fixed)
    active_ = false;
    FILE *f = fopen(calpath.c_str(), "r");
    if (!f) return;
    char line[256]; int i = 0;
    while (i < n && fgets(line, sizeof line, f)) {
      if (line[0] == '#') continue;
      int r, g, b;
      if (sscanf(line, "%d %d %d", &r, &g, &b) != 3) continue;
      r = clampPct(r); g = clampPct(g); b = clampPct(b);
      gr_[i] = r * 256 / 100; gg_[i] = g * 256 / 100; gb_[i] = b * 256 / 100;
      if (r != 100 || g != 100 || b != 100) active_ = true;
      ++i;
    }
    fclose(f);
    if (active_) fprintf(stderr, "Panel calibration loaded from %s.\n", calpath.c_str());
  }
  int width() const override { return target_->width(); }
  int height() const override { return target_->height(); }
  void Clear() override { target_->Clear(); }
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (active_) {
      const int idx = (y / panelH_) * gcols_ + (x / panelW_);
      if (idx >= 0 && idx < (int)gr_.size()) {
        r = (uint8_t)((r * gr_[idx]) >> 8);
        g = (uint8_t)((g * gg_[idx]) >> 8);
        b = (uint8_t)((b * gb_[idx]) >> 8);
      }
    }
    target_->SetPixel(x, y, r, g, b);
  }
  void Fill(uint8_t r, uint8_t g, uint8_t b) override {
    // Black stays black under any gain, and Fill(0,0,0) is by far the common
    // case (mode clears) -- keep that on the fast path.
    if (!active_ || (r == 0 && g == 0 && b == 0)) { target_->Fill(r, g, b); return; }
    for (int py = 0; py < grows_; ++py)
      for (int px = 0; px < gcols_; ++px) {
        const int idx = py * gcols_ + px;
        const uint8_t rr = (uint8_t)((r * gr_[idx]) >> 8), gg = (uint8_t)((g * gg_[idx]) >> 8),
                      bb = (uint8_t)((b * gb_[idx]) >> 8);
        const int x0 = px * panelW_, y0 = py * panelH_;
        for (int yy = 0; yy < panelH_; ++yy)
          for (int xx = 0; xx < panelW_; ++xx)
            target_->SetPixel(x0 + xx, y0 + yy, rr, gg, bb);
      }
  }
private:
  static int clampPct(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
  FrameCanvas *target_ = nullptr;
  int panelW_ = 64, panelH_ = 64, gcols_ = 1, grows_ = 1;
  bool active_ = false;
  std::vector<int> gr_, gg_, gb_;
};

int main() {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  matrix_options.hardware_mapping = "regular";

  // We ship a statically-linked binary (so it runs across Pi glibc versions),
  // and NSS lookups like getpwnam("daemon") are unreliable when static, so we
  // never drop privileges.
  runtime_opt.drop_privileges = 0;

  // Everything is configured from led-matrix-server.conf next to the binary --
  // there are no command-line arguments.
  const std::map<std::string, std::string> conf = ReadConf(ExeDir() + "/led-matrix-server.conf");
  // Mirror every entry into the environment as well, so modes that read secrets
  // via getenv (e.g. Jellyfin's host/key) pick them up.
  for (std::map<std::string, std::string>::const_iterator it = conf.begin(); it != conf.end(); ++it)
    setenv(it->first.c_str(), it->second.c_str(), 1);

  matrix_options.rows         = ConfInt(conf, "LED_ROWS", 64);
  matrix_options.cols         = ConfInt(conf, "LED_COLS", 64);
  matrix_options.chain_length = ConfInt(conf, "LED_CHAIN", 3);
  matrix_options.parallel     = ConfInt(conf, "LED_PARALLEL", 2);
  runtime_opt.gpio_slowdown   = ConfInt(conf, "LED_SLOWDOWN_GPIO", 3);

  g_web_port  = ConfInt(conf, "WEB_PORT", 8080);
  g_web_auth  = ConfStr(conf, "WEB_AUTH");
  g_tls_cert  = ConfStr(conf, "TLS_CERT");
  g_tls_key   = ConfStr(conf, "TLS_KEY");

  int start_mode = ConfInt(conf, "START_MODE", 1);
  int fps        = ConfInt(conf, "FPS", 60);
  if (fps <= 0) fps = 60;
  float speed = 1.0f;
  { const std::string s = ConfStr(conf, "SPEED"); if (!s.empty()) speed = atof(s.c_str()); }

  // HTTPS: a cert AND key must both be set. Default (neither) is plain HTTP.
  if (g_tls_cert.empty() != g_tls_key.empty()) {
    fprintf(stderr, "error: TLS_CERT and TLS_KEY must be set together (cert + key).\n");
    return 1;
  }
  if (!g_tls_cert.empty()) {
#ifdef USE_TLS
    SSL_library_init();
    SSL_load_error_strings();
    g_ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_ssl_ctx) { ERR_print_errors_fp(stderr); return 1; }
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(g_ssl_ctx, g_tls_cert.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(g_ssl_ctx, g_tls_key.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(g_ssl_ctx) != 1) {
      fprintf(stderr, "error: could not load TLS cert/key (%s, %s):\n",
              g_tls_cert.c_str(), g_tls_key.c_str());
      ERR_print_errors_fp(stderr);
      return 1;
    }
#else
    fprintf(stderr, "error: TLS_CERT/TLS_KEY set but this binary was built without TLS.\n"
                    "       Rebuild with `make TLS=1` for HTTPS support.\n");
    return 1;
#endif
  }

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) { fprintf(stderr, "error: could not initialize the LED matrix.\n"); return 1; }

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);
  signal(SIGPIPE, SIG_IGN);  // don't die if a web client disconnects mid-response

  srand((unsigned)time(NULL));

  const int width = matrix->width();
  const int height = matrix->height();

  // Video-mode source resolution (Jellyfin transcode target / MAME Xvfb size):
  // derived from the panel, not configured. 16:9 at the panel width, even dims.
  g_video_w = width;
  g_video_h = ((width * 9 / 16) + 1) & ~1;

  std::vector<Mode *> modes = BuildModes();   // built by the mode registry
  g_nbuiltin = modes.size();
  g_modes = &modes;   // let the control server route args (e.g. ticker symbols)
  RebuildModuleModes(modes);   // append whatever modules are installed on disk
  modules::EnsureCatalog();    // fetch the fixed GitHub catalog (background)
  LoadCycleConfig();  // restore persisted cycle set + cycle time (default: all modes)
  LoadArgs();         // restore per-mode arguments and push them into the modes

  if (start_mode < 1 || start_mode > (int)modes.size()) start_mode = 1;
  g_current_mode.store(start_mode - 1);

  std::thread web(WebServerThread, g_web_port);
  web.detach();

  FrameCanvas *offscreen = matrix->CreateFrameCanvas();

  // Per-panel calibration layer: modes draw into `cal`, which scales each pixel
  // by its panel's saved R/G/B gain and forwards to `offscreen`.
  CalibratedCanvas cal;
  {
    const std::string home = getenv("HOME") ? getenv("HOME") : "/root";
    cal.Configure(matrix_options.cols, matrix_options.rows,
                  matrix_options.chain_length, matrix_options.parallel,
                  home + "/led-matrix-server/panels.cal");
  }

  const long frame_us = 1000000L / fps;
  int last_mode = -1;
  float sim_t = 0.0f;
  const float dt = 1.0f / fps;
  bool was_cycling = false;
  time_t cycle_last = time(NULL);

  fprintf(stderr, "Rendering on %dx%d canvas at %d fps. Press CTRL-C to exit.\n",
          width, height, fps);

  while (!interrupt_received) {
    if (g_cycle.load()) {                       // mode 0: auto-advance on a timer
      const time_t now = time(NULL);
      if (!was_cycling) cycle_last = now;
      if (now - cycle_last >= g_cycle_secs.load()) {
        cycle_last = now;
        g_current_mode.store(NextChecked(g_current_mode.load(), (int)modes.size()));
      }
    }
    was_cycling = g_cycle.load();
    if (modules::TakeDirty()) {            // modules installed/removed via the web UI
      if (last_mode >= 0 && last_mode < (int)modes.size()) modes[last_mode]->Deactivate();
      last_mode = -1;                      // force a re-Activate; the vector just changed
      RebuildModuleModes(modes);
      if (g_current_mode.load() >= (int)modes.size()) g_current_mode.store(0);
      LoadCycleConfig();                   // re-resolve the checked set against new indices
      const std::vector<std::string> fresh = modules::TakeNewlyAdded();
      if (!fresh.empty()) {                  // a module you just installed cycles by default
        std::lock_guard<std::recursive_mutex> modelk(g_modes_mtx);
        FitChecked(false);
        for (size_t f = 0; f < fresh.size(); ++f)
          for (size_t i = 0; i < g_mode_names.size(); ++i)
            if (g_mode_names[i] == fresh[f]) g_checked[i] = 1;
        SaveCycleConfig();
      }
    }
    const int m = g_current_mode.load();
    if (m != last_mode) {
      if (last_mode >= 0) modes[last_mode]->Deactivate();
      modes[m]->Activate(width, height);
      last_mode = m;
    }
    const float sdt = dt * speed;
    sim_t += sdt;
    cal.SetTarget(offscreen);
    modes[m]->Draw(&cal, sim_t, sdt);
    offscreen = matrix->SwapOnVSync(offscreen);
    usleep(frame_us);
  }

  fprintf(stderr, "\nCaught exit signal. Shutting down cleanly.\n");
  g_running.store(false);
  matrix->Clear();
  delete matrix;
  for (Mode *m : modes) delete m;
  return 0;
}
