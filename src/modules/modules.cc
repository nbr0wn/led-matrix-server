// Fetched Lua modules. See modules.h for the catalog format and threading.
#include "modules.h"
#include "json.h"
#include "cpp_modes/lua/lua-engine.h"

#include <errno.h>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace modules {
namespace {

std::mutex g_mtx;                 // guards the on-disk state below
bool g_dirty = false;
std::vector<std::string> g_newly_added;

// --------------------------------------------------------------------------
// Paths. Everything lives under ~/led-matrix-server/modules, next to panels.cal.
// --------------------------------------------------------------------------
std::string Home() {
  const char *h = getenv("HOME");
  return std::string(h ? h : "/root");
}
std::string Root()        { return Home() + "/led-matrix-server/modules"; }
std::string CatalogsPath(){ return Root() + "/catalogs.conf"; }
std::string InstalledPath(){ return Root() + "/installed.conf"; }
std::string CacheDir()    { return Root() + "/cache"; }
std::string ScriptDir()   { return Root() + "/scripts"; }

void EnsureDirs() {
  mkdir((Home() + "/led-matrix-server").c_str(), 0755);
  mkdir(Root().c_str(), 0755);
  mkdir(CacheDir().c_str(), 0755);
  mkdir(ScriptDir().c_str(), 0755);
}

// Stable short name for a URL, so a catalog's cache file doesn't depend on
// anything unsafe in the URL itself. FNV-1a.
std::string Hash(const std::string &s) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < s.size(); ++i) { h ^= (uint8_t)s[i]; h *= 1099511628211ull; }
  char b[20];
  snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
  return b;
}
// Module ids reach the filesystem, so keep them to a boring alphabet.
std::string SafeId(const std::string &id) {
  std::string o;
  for (size_t i = 0; i < id.size() && i < 48; ++i) {
    const char c = id[i];
    o += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '-' || c == '_') ? c : '_';
  }
  return o.empty() ? "_" : o;
}
std::string ScriptPath(const std::string &url, const std::string &id) {
  return ScriptDir() + "/" + Hash(url) + "-" + SafeId(id) + ".lua";
}

// --------------------------------------------------------------------------
// Small file helpers.
// --------------------------------------------------------------------------
std::string ReadFile(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char b[8192]; size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
  fclose(f);
  return s;
}
bool WriteFile(const std::string &p, const std::string &data) {
  FILE *f = fopen(p.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  return ok;
}
std::vector<std::string> ReadLines(const std::string &p) {
  std::vector<std::string> out;
  const std::string s = ReadFile(p);
  size_t i = 0;
  while (i < s.size()) {
    size_t e = s.find('\n', i);
    if (e == std::string::npos) e = s.size();
    std::string line = s.substr(i, e - i);
    while (!line.empty() && (line[line.size()-1] == '\r' || line[line.size()-1] == ' '))
      line.erase(line.size() - 1);
    if (!line.empty()) out.push_back(line);
    i = e + 1;
  }
  return out;
}

// --------------------------------------------------------------------------
// Fetching. curl is already a dependency (the stock ticker uses it) and gives
// us TLS and redirects for free.
// --------------------------------------------------------------------------

// Only plain http(s) URLs, and nothing that could break out of the shell
// quoting below. Anything else is refused rather than escaped.
bool UrlOk(const std::string &u) {
  if (u.compare(0, 7, "http://") != 0 && u.compare(0, 8, "https://") != 0) return false;
  if (u.size() > 2000) return false;
  for (size_t i = 0; i < u.size(); ++i) {
    const unsigned char c = (unsigned char)u[i];
    if (c <= 32 || c >= 127 || c == '\'' || c == '\\' || c == '"' || c == '`' ||
        c == '$' || c == ';' || c == '|' || c == '&' || c == '<' || c == '>')
      return false;
  }
  return true;
}

// GET `url`, up to `limit` bytes. Returns "" and sets *err on failure.
std::string Fetch(const std::string &url, size_t limit, std::string *err) {
  if (!UrlOk(url)) { *err = "refusing URL (must be a plain http/https URL)"; return ""; }
  const std::string cmd = "curl -fsSL --max-time 25 --max-filesize " +
                          std::to_string((long long)limit) + " -- '" + url + "' 2>/dev/null";
  FILE *p = popen(cmd.c_str(), "r");
  if (!p) { *err = "cannot run curl"; return ""; }
  std::string out;
  char b[8192]; size_t n;
  while ((n = fread(b, 1, sizeof b, p)) > 0) {
    out.append(b, n);
    if (out.size() > limit) break;
  }
  const int rc = pclose(p);
  if (rc != 0) { *err = "fetch failed (curl exit " + std::to_string(WEXITSTATUS(rc)) + ")"; return ""; }
  if (out.empty()) { *err = "empty response"; return ""; }
  err->clear();
  return out;
}

// Resolve a manifest-relative path against the manifest's own URL.
std::string ResolveUrl(const std::string &base, const std::string &rel) {
  if (rel.empty()) return "";
  if (rel.compare(0, 7, "http://") == 0 || rel.compare(0, 8, "https://") == 0) return rel;
  const size_t scheme = base.find("://");
  if (scheme == std::string::npos) return rel;
  if (rel[0] == '/') {                                   // host-absolute
    const size_t host_end = base.find('/', scheme + 3);
    return (host_end == std::string::npos ? base : base.substr(0, host_end)) + rel;
  }
  size_t cut = base.rfind('/');                          // strip the manifest filename
  if (cut == std::string::npos || cut < scheme + 3) return base + "/" + rel;
  return base.substr(0, cut + 1) + rel;
}

// --------------------------------------------------------------------------
// Catalog + installed-set persistence.
// --------------------------------------------------------------------------
// The catalog is fixed to the project's GitHub repo -- there is no chooser, so
// this is always the single source (catalogs.conf is no longer consulted).
const char *kCatalogUrl =
    "https://raw.githubusercontent.com/nbr0wn/led-matrix-server-lua/main/catalog.json";
std::vector<std::string> UrlsLocked() { return { std::string(kCatalogUrl) }; }

void SaveUrlsLocked(const std::vector<std::string> &urls) {
  std::string s;
  for (size_t i = 0; i < urls.size(); ++i) s += urls[i] + "\n";
  WriteFile(CatalogsPath(), s);
}

struct Install { std::string url, id, name; };

std::vector<Install> InstalledLocked() {
  std::vector<Install> out;
  const std::vector<std::string> lines = ReadLines(InstalledPath());
  for (size_t i = 0; i < lines.size(); ++i) {
    const size_t t1 = lines[i].find('\t');
    if (t1 == std::string::npos) continue;
    const size_t t2 = lines[i].find('\t', t1 + 1);
    Install in;
    in.url = lines[i].substr(0, t1);
    in.id = (t2 == std::string::npos) ? lines[i].substr(t1 + 1) : lines[i].substr(t1 + 1, t2 - t1 - 1);
    in.name = (t2 == std::string::npos) ? in.id : lines[i].substr(t2 + 1);
    out.push_back(in);
  }
  return out;
}

void SaveInstalledLocked(const std::vector<Install> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += v[i].url + "\t" + v[i].id + "\t" + v[i].name + "\n";
  WriteFile(InstalledPath(), s);
}

// Parse a cached manifest into a catalog description.
CatalogInfo ParseCatalog(const std::string &url, const std::string &text) {
  CatalogInfo cat;
  cat.url = url;
  if (text.empty()) { cat.error = "not fetched yet"; return cat; }
  Json root;
  std::string err;
  if (!Json::Parse(text, &root, &err)) { cat.error = "bad JSON: " + err; return cat; }
  if (root.type != Json::OBJ) { cat.error = "manifest is not a JSON object"; return cat; }
  cat.name = root.Str("name", "");
  const Json *mods = root.Get("modules");
  if (!mods || mods->type != Json::ARR) { cat.error = "manifest has no \"modules\" array"; return cat; }
  for (size_t i = 0; i < mods->arr.size(); ++i) {
    const Json &m = mods->arr[i];
    if (m.type != Json::OBJ) continue;
    ModuleInfo mi;
    mi.id = m.Str("id", "");
    if (mi.id.empty()) continue;                       // an id is what we key on
    mi.name = m.Str("name", mi.id.c_str());
    mi.desc = m.Str("desc", "");
    mi.script_url = ResolveUrl(url, m.Str("script", ""));
    mi.shot_url = ResolveUrl(url, m.Str("shot", ""));
    if (mi.script_url.empty()) continue;               // nothing to run
    cat.modules.push_back(mi);
  }
  if (cat.modules.empty() && cat.error.empty()) cat.error = "no usable modules in manifest";
  return cat;
}

std::string FetchCatalogLocked(const std::string &url) {
  std::string err;
  const std::string text = Fetch(url, 1u << 20, &err);
  if (!err.empty()) return err;
  const CatalogInfo cat = ParseCatalog(url, text);
  if (!cat.error.empty()) return cat.error;
  WriteFile(CacheDir() + "/" + Hash(url) + ".json", text);
  return "";
}

// --------------------------------------------------------------------------
// The mode wrapper. One per installed module; runs its script on the shared
// engine, reloading from disk each time the mode is selected so a re-fetch
// takes effect and every visit restarts the animation cleanly.
// --------------------------------------------------------------------------
class ModuleMode : public LuaScriptMode {
public:
  ModuleMode(const std::string &name, const std::string &path)
      : label_(name), path_(path) {
    SetArgHint(DetectArgHint(ReadFile(path)));       // so the UI shows an arg box
  }
  const char *name() const override { return label_.c_str(); }
  void Activate(int w, int h) override {
    LuaScriptMode::Activate(w, h);
    const std::string src = ReadFile(path_);
    SetScript(src.empty() ? "-- module script missing or empty\n" : src);
  }
  std::string SourceText() override { return ReadFile(path_); }   // for the pen button
private:
  std::string label_, path_;
};

}  // namespace

// The value a checkbox carries. Deliberately free of whitespace: a form value
// containing a newline comes back CRLF-normalised by the browser, which would
// never match what we wrote out.
std::string Key(const std::string &catalog_url, const std::string &id) {
  return Hash(catalog_url) + ":" + SafeId(id);
}

std::vector<CatalogInfo> Catalogs() {
  std::lock_guard<std::mutex> lk(g_mtx);
  EnsureDirs();
  const std::vector<std::string> urls = UrlsLocked();
  const std::vector<Install> inst = InstalledLocked();
  std::vector<CatalogInfo> out;
  for (size_t i = 0; i < urls.size(); ++i) {
    CatalogInfo cat = ParseCatalog(urls[i], ReadFile(CacheDir() + "/" + Hash(urls[i]) + ".json"));
    for (size_t m = 0; m < cat.modules.size(); ++m)
      for (size_t k = 0; k < inst.size(); ++k)
        if (inst[k].url == urls[i] && inst[k].id == cat.modules[m].id) {
          cat.modules[m].installed = true;
          break;
        }
    out.push_back(cat);
  }
  return out;
}

std::string AddCatalog(const std::string &url) {
  std::lock_guard<std::mutex> lk(g_mtx);
  EnsureDirs();
  if (!UrlOk(url)) return "That doesn't look like an http(s) URL.";
  std::vector<std::string> urls = UrlsLocked();
  for (size_t i = 0; i < urls.size(); ++i)
    if (urls[i] == url) return FetchCatalogLocked(url);       // already known: just refresh
  const std::string err = FetchCatalogLocked(url);
  if (!err.empty()) return err;
  urls.push_back(url);
  SaveUrlsLocked(urls);
  return "";
}

void RemoveCatalog(const std::string &url) {
  std::lock_guard<std::mutex> lk(g_mtx);
  std::vector<std::string> urls = UrlsLocked(), keep;
  for (size_t i = 0; i < urls.size(); ++i) if (urls[i] != url) keep.push_back(urls[i]);
  SaveUrlsLocked(keep);
  unlink((CacheDir() + "/" + Hash(url) + ".json").c_str());

  // Uninstall whatever came from it -- otherwise those modes would be stranded:
  // still in the mode list, with no tile left to untick them from.
  const std::vector<Install> before = InstalledLocked();
  std::vector<Install> after;
  bool changed = false;
  for (size_t i = 0; i < before.size(); ++i) {
    if (before[i].url == url) {
      unlink(ScriptPath(before[i].url, before[i].id).c_str());
      changed = true;
    } else {
      after.push_back(before[i]);
    }
  }
  if (changed) { SaveInstalledLocked(after); g_dirty = true; }
}

std::string RefreshCatalogs() {
  std::lock_guard<std::mutex> lk(g_mtx);
  EnsureDirs();
  const std::vector<std::string> urls = UrlsLocked();
  std::string errs;
  for (size_t i = 0; i < urls.size(); ++i) {
    const std::string e = FetchCatalogLocked(urls[i]);
    if (!e.empty()) errs += (errs.empty() ? "" : "; ") + urls[i] + ": " + e;
  }
  return errs;
}

// Fetch the fixed GitHub catalog once at startup if it isn't cached, in the
// background so a slow/absent network never delays the panel coming up.
void EnsureCatalog() {
  std::thread([] {
    std::lock_guard<std::mutex> lk(g_mtx);
    EnsureDirs();
    if (ReadFile(CacheDir() + "/" + Hash(kCatalogUrl) + ".json").empty())
      FetchCatalogLocked(kCatalogUrl);
  }).detach();
}

std::string ApplySelection(const std::vector<std::string> &keys) {
  std::lock_guard<std::mutex> lk(g_mtx);
  EnsureDirs();

  // Everything on offer right now, so a checked key can be resolved to a URL.
  std::vector<ModuleInfo> avail;
  std::vector<std::string> avail_url, readable;
  const std::vector<std::string> urls = UrlsLocked();
  for (size_t i = 0; i < urls.size(); ++i) {
    const CatalogInfo cat = ParseCatalog(urls[i], ReadFile(CacheDir() + "/" + Hash(urls[i]) + ".json"));
    if (!cat.error.empty()) continue;              // unreadable: leave its modules alone
    readable.push_back(urls[i]);
    for (size_t m = 0; m < cat.modules.size(); ++m) {
      avail.push_back(cat.modules[m]);
      avail_url.push_back(urls[i]);
    }
  }

  const std::vector<Install> before = InstalledLocked();
  std::vector<Install> after;
  std::string errs;
  int added = 0, removed = 0;

  // Newly checked -> download. Already installed -> keep (and keep its order).
  for (size_t k = 0; k < keys.size(); ++k) {
    size_t a = avail.size();
    for (size_t i = 0; i < avail.size(); ++i)
      if (Key(avail_url[i], avail[i].id) == keys[k]) { a = i; break; }
    if (a == avail.size()) continue;                    // stale checkbox; ignore

    bool have = false;
    for (size_t b = 0; b < before.size(); ++b)
      if (before[b].url == avail_url[a] && before[b].id == avail[a].id) { have = true; break; }

    const std::string path = ScriptPath(avail_url[a], avail[a].id);
    if (!have || ReadFile(path).empty()) {
      std::string err;
      const std::string src = Fetch(avail[a].script_url, 1u << 20, &err);
      if (!err.empty()) {
        errs += (errs.empty() ? "" : "; ") + avail[a].name + ": " + err;
        continue;
      }
      const std::string bad = LuaScriptMode::SyntaxCheck(src);
      if (!bad.empty()) {
        errs += (errs.empty() ? "" : "; ") + avail[a].name + ": " + bad;
        continue;
      }
      if (!WriteFile(path, src)) {
        errs += (errs.empty() ? "" : "; ") + avail[a].name + ": cannot write script";
        continue;
      }
      if (!have) { ++added; g_newly_added.push_back(avail[a].name); }
    }
    Install in;
    in.url = avail_url[a]; in.id = avail[a].id; in.name = avail[a].name;
    after.push_back(in);
  }

  // Anything installed before and not checked now goes away, script and all --
  // unless its catalog was unreadable this time round, in which case the user
  // never saw a tile for it and cannot have meant to untick it.
  for (size_t b = 0; b < before.size(); ++b) {
    bool keep = false;
    for (size_t a = 0; a < after.size(); ++a)
      if (after[a].url == before[b].url && after[a].id == before[b].id) { keep = true; break; }
    if (!keep) {
      bool visible = false;
      for (size_t r = 0; r < readable.size(); ++r)
        if (readable[r] == before[b].url) { visible = true; break; }
      if (!visible) { after.push_back(before[b]); continue; }   // keep it, untouched
      unlink(ScriptPath(before[b].url, before[b].id).c_str());
      ++removed;
    }
  }

  SaveInstalledLocked(after);
  g_dirty = true;                       // render thread rebuilds the mode list
  if (!errs.empty()) return errs;
  if (added || removed) {
    char b[128];
    snprintf(b, sizeof b, "%d module%s added, %d removed.", added, added == 1 ? "" : "s", removed);
    return std::string("\1") + b;       // \1 marks "not an error" for the caller
  }
  return "";
}

std::vector<std::string> TakeNewlyAdded() {
  std::lock_guard<std::mutex> lk(g_mtx);
  std::vector<std::string> out;
  out.swap(g_newly_added);
  return out;
}

bool TakeDirty() {
  std::lock_guard<std::mutex> lk(g_mtx);
  const bool d = g_dirty;
  g_dirty = false;
  return d;
}

std::vector<Mode *> BuildModes() {
  std::lock_guard<std::mutex> lk(g_mtx);
  std::vector<Mode *> out;
  const std::vector<Install> inst = InstalledLocked();
  for (size_t i = 0; i < inst.size(); ++i)
    out.push_back(new ModuleMode(inst[i].name, ScriptPath(inst[i].url, inst[i].id)));
  return out;
}

size_t InstalledCount() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return InstalledLocked().size();
}

}  // namespace modules
