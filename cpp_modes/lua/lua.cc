// LuaMode -- registered as mode 36.
#include "lua-engine.h"

// --- 37: Lua scripting ----------------------------------------------------
// The user's scratchpad script: pasted into the web UI or POSTed to /api/lua
// (see led-matrix-server.cc), persisted so it survives a restart. The script itself
// runs on the shared engine in lua-engine.h; this mode adds the persistence,
// the LuaHost hooks the web server drives it through, and the example script.
class LuaMode : public LuaScriptMode, public LuaHost {
public:
  LuaMode() {
    g_lua_host = this;
    std::string saved = LoadPersisted();          // restore last script across restarts
    if (saved.empty()) saved = DefaultScript();   // ...or ship a default so it's never blank
    SetScript(saved);
  }
  const char *name() const override { return "Custom LUA script"; }

  // The built-in default program: a warp starfield on black. Also what the web
  // UI's "Insert example" button pastes, so it doubles as a live API example.
  static const char *DefaultScript() {
    return
      "-- Warp starfield. setup() seeds the stars; loop() flies them past you.\n"
      "-- Globals set in setup() (W, H, cx, cy, stars) persist into loop().\n"
      "local N = 2000           -- number of stars\n"
      "local SPEED = 6.0        -- how fast they rush toward you\n"
      "\n"
      "local function newstar(far)\n"
      "  return {\n"
      "    x = (math.random() - 0.5) * W * 2,\n"
      "    y = (math.random() - 0.5) * H * 2,\n"
      "    z = far and (math.random() * W) or W,   -- start deep, or all the way back\n"
      "    r = 160 + math.random(0, 95),\n"
      "    g = 160 + math.random(0, 95),\n"
      "    b = 200 + math.random(0, 55),           -- stars lean slightly blue-white\n"
      "  }\n"
      "end\n"
      "\n"
      "function setup()\n"
      "  W, H = width(), height()\n"
      "  cx, cy = W / 2, H / 2\n"
      "  stars = {}\n"
      "  for i = 1, N do stars[i] = newstar(true) end\n"
      "end\n"
      "\n"
      "function loop(t, frame)\n"
      "  clear(0, 0, 0)                             -- black background every frame\n"
      "  for i = 1, N do\n"
      "    local s = stars[i]\n"
      "    local pz = s.z                           -- previous depth (for the streak)\n"
      "    s.z = s.z - SPEED\n"
      "    if s.z <= 1 then s = newstar(false); stars[i] = s; pz = s.z + SPEED end\n"
      "    local k, pk = 100 / s.z, 100 / pz\n"
      "    local px = math.floor(cx + s.x * k)\n"
      "    local py = math.floor(cy + s.y * k)\n"
      "    local qx = math.floor(cx + s.x * pk)\n"
      "    local qy = math.floor(cy + s.y * pk)\n"
      "    local f = 1 - s.z / W                     -- nearer == brighter\n"
      "    if f < 0 then f = 0 end\n"
      "    line(qx, qy, px, py, math.floor(s.r*f), math.floor(s.g*f), math.floor(s.b*f))\n"
      "  end\n"
      "end\n";
  }

  Mode *AsMode() override { return this; }
  // Syntax-check `src`; on success persist it to disk and queue it for the
  // render thread. Returns "OK\n" or "ERROR: ...\n" (the web server maps that
  // to its HTTP reply).
  std::string Submit(const std::string &src) override {
    const std::string err = SyntaxCheck(src);
    if (!err.empty()) return "ERROR: " + err + "\n";
    SetScript(src);
    Persist(src);
    return "OK\n";
  }
  std::string Current() override { return Script(); }
  std::string LastError() override { return Error(); }

private:
  // Where the last-uploaded script lives so it survives a restart. Sits next to
  // panels.cal under $HOME/led-matrix-server.
  static std::string ScriptPath() {
    const char *h = getenv("HOME");
    return std::string(h ? h : "/root") + "/led-matrix-server/lua_script.lua";
  }
  void Persist(const std::string &src) {
    FILE *f = fopen(ScriptPath().c_str(), "w");
    if (!f) return;
    fwrite(src.data(), 1, src.size(), f);
    fclose(f);
  }
  static std::string LoadPersisted() {
    FILE *f = fopen(ScriptPath().c_str(), "r");
    if (!f) return "";
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
  }
};

const char *LuaHost::DefaultScript() { return LuaMode::DefaultScript(); }

REGISTER_MODE(35, LuaMode);
