// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// The sandboxed Lua display engine, shared by the built-in "Lua Script"
// scratchpad mode and by fetched modules (src/modules/). A script defines
// globals, an optional setup() called once, and loop(t, frame) called every
// frame; it draws through the primitive API registered below. Sandboxed to
// base/table/string/math -- no io/os/package/debug -- and an instruction-count
// hook aborts runaway loops so a bad script cannot wedge the panel.
//
//   api:  w=width() h=height()  clear([r,g,b])  color(r,g,b)  setpixel(x,y[,r,g,b])
//         line(x0,y0,x1,y1[,r,g,b])  rect(x,y,w,h[,r,g,b])  circle(x,y,rad[,r,g,b])
//         triangle(x0,y0,x1,y1,x2,y2[,r,g,b])  filltriangle(x0,y0,x1,y1,x2,y2[,r,g,b])
//         text(x,y,str[,r,g,b])  scrollh(n)  scrollv(n)
//   color(r,g,b) sets the pen; any later call may omit its r,g,b to use it.
#ifndef LED_MATRIX_SERVER_LUA_ENGINE_H
#define LED_MATRIX_SERVER_LUA_ENGINE_H

#include "common/mode.h"

#include <mutex>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// A Canvas backed by a plain RGB byte buffer, so text (BDF font) can be drawn
// into a script's persistent frame buffer.
class BufCanvas : public Canvas {
public:
  BufCanvas(uint8_t *b, int w, int h) : b_(b), w_(w), h_(h) {}
  int width() const override { return w_; }
  int height() const override { return h_; }
  void Clear() override { memset(b_, 0, (size_t)w_ * h_ * 3); }
  void Fill(uint8_t r, uint8_t g, uint8_t bl) override {
    for (int i = 0; i < w_ * h_; ++i) { b_[i*3] = r; b_[i*3+1] = g; b_[i*3+2] = bl; }
  }
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t bl) override {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
    uint8_t *p = b_ + ((size_t)y*w_ + x)*3; p[0] = r; p[1] = g; p[2] = bl;
  }
private:
  uint8_t *b_; int w_, h_;
};


class LuaScriptMode : public Mode {
public:
  ~LuaScriptMode() override { if (L_) lua_close(L_); }

  // Queue `src` for the render thread; it is compiled at the top of the next
  // frame (never on the caller's thread, which may be a socket handler).
  void SetScript(const std::string &src) {
    std::lock_guard<std::mutex> lk(mtx_);
    pending_ = src; hasPending_ = true;
  }
  // The script currently loaded (or queued), for pre-filling an editor.
  std::string Script() { std::lock_guard<std::mutex> lk(mtx_); return pending_; }
  // The current runtime error (from setup()/loop()), or empty. Read off the web
  // thread; err_ is only ever written on the render thread, under errMtx_.
  std::string Error() { std::lock_guard<std::mutex> lk(errMtx_); return err_; }
  std::string StatusLine() override { return Error(); }

  // Arguments: a script that sets a top-level `arghint` string takes arguments
  // (KEY=VALUE pairs). The web UI / API set them via SetArg; the script is then
  // reloaded so setup(args) runs again with the new values.
  bool TakesArg() const override { return !argHint_.empty(); }
  const char *ArgHint() const override { return argHint_.c_str(); }
  const char *ArgUsage() const override {
    return "KEY=VALUE pairs, space-separated; quote values with spaces "
           "(e.g. symbol=TSLA range=5d). Passed to the script's setup(args).";
  }
  void SetArg(const std::string &a) override {
    std::lock_guard<std::mutex> lk(mtx_);
    argRaw_ = a;
    if (!pending_.empty()) hasPending_ = true;       // reload so setup() re-runs
  }
  // Pre-seed the hint (used by fetched modules so the UI knows before Activate).
  void SetArgHint(const std::string &h) { argHint_ = h; }

  // Compile-check `src` without disturbing the running script. Returns the
  // error text, or "" if it parses.
  static std::string SyntaxCheck(const std::string &src) {
    lua_State *T = luaL_newstate();
    if (!T) return "no memory";
    std::string err;
    if (luaL_loadstring(T, src.c_str()) != LUA_OK) {
      const char *e = lua_tostring(T, -1);
      err = e ? e : "syntax error";
    }
    lua_close(T);
    return err;
  }

  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    if ((int)buf_.size() != w*h*3) buf_.assign((size_t)w*h*3, 0);
    if (!fontTried_) {
      fontTried_ = true;
      const char *c[] = { "fonts/6x10.bdf", "/root/led-matrix-server/fonts/6x10.bdf" };
      for (const char *p : c) if (font_.LoadFont(p)) { fontOK_ = true; break; }
    }
  }
  void Draw(Canvas *c, float t, float dt) override {
    s_cur = this;
    { std::lock_guard<std::mutex> lk(mtx_); if (hasPending_) { LoadScript(pending_); hasPending_ = false; } }
    if (L_ && err_.empty() && haveLoop_) {
      lua_getglobal(L_, "loop");
      lua_pushnumber(L_, t);
      lua_pushinteger(L_, frame_);
      lua_sethook(L_, InstrHook, LUA_MASKCOUNT, 20000000);
      if (lua_pcall(L_, 2, 0, 0) != LUA_OK) { SetErr(); }
      lua_sethook(L_, NULL, 0, 0);
    }
    ++frame_;
    s_cur = nullptr;
    size_t i = 0;                                    // blit buffer -> panel
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, i += 3)
        c->SetPixel(x, y, buf_[i], buf_[i+1], buf_[i+2]);
    if (fontOK_) {
      if (!err_.empty())
        rgb_matrix::DrawText(c, font_, 1, font_.baseline(), rgb_matrix::Color(255,60,60),
                             ("ERR: " + err_).c_str());
      else if (!L_)
        rgb_matrix::DrawText(c, font_, 1, font_.baseline(), rgb_matrix::Color(150,150,200),
                             status_.c_str());
    }
  }
protected:
  void SetErr() {
    const char *e = lua_tostring(L_, -1);
    const std::string m = e ? e : "error";       // copy before pop invalidates it
    lua_pop(L_, 1);
    std::lock_guard<std::mutex> lk(errMtx_);
    err_ = m;
  }
  void ClearErr() { std::lock_guard<std::mutex> lk(errMtx_); err_.clear(); }
  void LoadScript(const std::string &src) {
    if (L_) { lua_close(L_); L_ = nullptr; }
    ClearErr(); haveLoop_ = false; frame_ = 0;
    curR_ = curG_ = curB_ = 255;                  // pen resets to white per script
    std::fill(buf_.begin(), buf_.end(), 0);
    L_ = luaL_newstate();
    if (!L_) { std::lock_guard<std::mutex> lk(errMtx_); err_ = "no memory"; return; }
    OpenSafeLibs(L_);
    RegisterApi(L_);
    lua_sethook(L_, InstrHook, LUA_MASKCOUNT, 20000000);
    const int rc = luaL_dostring(L_, src.c_str());   // define setup()/loop() + globals
    lua_sethook(L_, NULL, 0, 0);
    if (rc != LUA_OK) { SetErr(); return; }
    // A script that wants arguments sets a top-level `arghint` string; its
    // presence is what makes the mode take an argument (see TakesArg()).
    lua_getglobal(L_, "arghint");
    argHint_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "";
    lua_pop(L_, 1);
    lua_getglobal(L_, "setup");                      // call setup(args) once, if present
    if (lua_isfunction(L_, -1)) {
      PushArgs(L_, argRaw_);                         // named KEY=VALUE args as a table
      lua_sethook(L_, InstrHook, LUA_MASKCOUNT, 20000000);
      const int src_rc = lua_pcall(L_, 1, 0, 0);
      lua_sethook(L_, NULL, 0, 0);
      if (src_rc != LUA_OK) { SetErr(); return; }
    } else {
      lua_pop(L_, 1);                                // discard the non-function value
    }
    lua_getglobal(L_, "loop");
    haveLoop_ = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    status_ = haveLoop_ ? "running loop()" : "no loop() defined";
  }
  static void OpenSafeLibs(lua_State *L) {   // no io/os/package/debug (sandbox)
    static const luaL_Reg libs[] = {
      {"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
      {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {NULL, NULL} };
    for (const luaL_Reg *l = libs; l->func; ++l) { luaL_requiref(L, l->name, l->func, 1); lua_pop(L, 1); }
  }
  static void RegisterApi(lua_State *L) {
    static const luaL_Reg api[] = {
      {"width", l_width}, {"height", l_height}, {"clear", l_clear}, {"setpixel", l_setpixel},
      {"line", l_line}, {"rect", l_rect}, {"circle", l_circle}, {"text", l_text},
      {"triangle", l_triangle}, {"filltriangle", l_fill_triangle}, {"color", l_color},
      {"scrollh", l_scrollh}, {"scrollv", l_scrollv}, {NULL, NULL} };
    for (const luaL_Reg *a = api; a->func; ++a) { lua_pushcfunction(L, a->func); lua_setglobal(L, a->name); }
  }
  static void InstrHook(lua_State *L, lua_Debug *) {
    luaL_error(L, "script exceeded instruction limit (infinite loop?)");
  }
  // Parse a "KEY=VALUE KEY2=VALUE2" argument string (values may be quoted to
  // hold spaces) and push it as a Lua table -- setup(args) receives it. A value
  // that parses cleanly as a number becomes a number, otherwise a string.
  static void PushArgs(lua_State *L, const std::string &raw) {
    lua_newtable(L);
    const size_t n = raw.size();
    size_t i = 0;
    while (i < n) {
      while (i < n && isspace((unsigned char)raw[i])) ++i;
      if (i >= n) break;
      const size_t ks = i;
      while (i < n && raw[i] != '=' && !isspace((unsigned char)raw[i])) ++i;
      const std::string key = raw.substr(ks, i - ks);
      std::string val;
      if (i < n && raw[i] == '=') {
        ++i;
        if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
          const char q = raw[i++]; const size_t vs = i;
          while (i < n && raw[i] != q) ++i;
          val = raw.substr(vs, i - vs);
          if (i < n) ++i;                            // skip closing quote
        } else {
          const size_t vs = i;
          while (i < n && !isspace((unsigned char)raw[i])) ++i;
          val = raw.substr(vs, i - vs);
        }
      }
      if (key.empty()) continue;
      char *end = nullptr;
      const double d = strtod(val.c_str(), &end);
      if (!val.empty() && end && *end == '\0') lua_pushnumber(L, d);
      else lua_pushlstring(L, val.data(), val.size());
      lua_setfield(L, -2, key.c_str());
    }
  }
  // Whether a script declares an `arghint` (so it takes arguments), and the
  // hint text, without disturbing a running script -- used by the web UI to
  // decide whether to show an argument box before the mode is ever activated.
  static std::string DetectArgHint(const std::string &src) {
    lua_State *T = luaL_newstate();
    if (!T) return "";
    OpenSafeLibs(T);
    RegisterApi(T);                                  // no-op API (s_cur is null)
    std::string hint;
    lua_sethook(T, InstrHook, LUA_MASKCOUNT, 20000000);
    if (luaL_dostring(T, src.c_str()) == LUA_OK) {
      lua_getglobal(T, "arghint");
      if (lua_isstring(T, -1)) hint = lua_tostring(T, -1);
    }
    lua_close(T);
    return hint;
  }
  static int opt(lua_State *L, int i, int def) {
    return lua_isnoneornil(L, i) ? def : (int)luaL_checkinteger(L, i);
  }
  // Colour argument that falls back to the current pen colour (set by color())
  // when omitted, so a call can either carry its own r,g,b or inherit the pen.
  static int col(lua_State *L, int i, int cur) {
    return lua_isnoneornil(L, i) ? cur : (int)luaL_checkinteger(L, i);
  }
  static int clampc(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
  static void put(int x, int y, int r, int g, int b) {
    LuaScriptMode *m = s_cur; if (!m) return;
    if (x < 0 || x >= m->width_ || y < 0 || y >= m->height_) return;
    uint8_t *p = &m->buf_[((size_t)y*m->width_ + x)*3];
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b;
  }
  // Bresenham segment, shared by line() and the triangle outline.
  static void seg(int x0, int y0, int x1, int y1, int r, int g, int b) {
    int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, e=dx+dy;
    for (;;) { put(x0,y0,r,g,b); if (x0==x1&&y0==y1) break; int e2=2*e;
      if (e2>=dy){e+=dy;x0+=sx;} if (e2<=dx){e+=dx;y0+=sy;} }
  }
  // Twice the signed area of triangle ABC; its sign says which side of AB C is
  // on. Used as an edge function to test pixel coverage. 64-bit so far-apart
  // vertices can't overflow the products.
  static long long orient(int ax, int ay, int bx, int by, int cx, int cy) {
    return (long long)(bx-ax)*(cy-ay) - (long long)(by-ay)*(cx-ax);
  }
  static int l_width(lua_State *L) { lua_pushinteger(L, s_cur ? s_cur->width_ : 0); return 1; }
  static int l_height(lua_State *L) { lua_pushinteger(L, s_cur ? s_cur->height_ : 0); return 1; }
  static int l_clear(lua_State *L) {
    const int r = opt(L,1,0), g = opt(L,2,0), b = opt(L,3,0);
    LuaScriptMode *m = s_cur; if (!m) return 0;
    for (size_t i = 0; i < m->buf_.size(); i += 3) { m->buf_[i]=r; m->buf_[i+1]=g; m->buf_[i+2]=b; }
    return 0;
  }
  // Set the current pen colour; every later drawing call with its colour
  // arguments omitted uses it, until color() is called again.
  static int l_color(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    m->curR_ = clampc((int)luaL_checkinteger(L,1));
    m->curG_ = clampc((int)luaL_checkinteger(L,2));
    m->curB_ = clampc((int)luaL_checkinteger(L,3));
    return 0;
  }
  static int l_setpixel(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    put((int)luaL_checkinteger(L,1), (int)luaL_checkinteger(L,2),
        col(L,3,m->curR_), col(L,4,m->curG_), col(L,5,m->curB_));
    return 0;
  }
  static int l_line(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    int x0=(int)luaL_checkinteger(L,1), y0=(int)luaL_checkinteger(L,2),
        x1=(int)luaL_checkinteger(L,3), y1=(int)luaL_checkinteger(L,4);
    seg(x0,y0,x1,y1, col(L,5,m->curR_), col(L,6,m->curG_), col(L,7,m->curB_));
    return 0;
  }
  static int l_rect(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    int x=(int)luaL_checkinteger(L,1), y=(int)luaL_checkinteger(L,2),
        w=(int)luaL_checkinteger(L,3), h=(int)luaL_checkinteger(L,4);
    const int r=col(L,5,m->curR_), g=col(L,6,m->curG_), b=col(L,7,m->curB_);
    for (int yy=0; yy<h; ++yy) for (int xx=0; xx<w; ++xx) put(x+xx, y+yy, r, g, b);
    return 0;
  }
  static int l_circle(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    int cx=(int)luaL_checkinteger(L,1), cy=(int)luaL_checkinteger(L,2), rad=(int)luaL_checkinteger(L,3);
    const int r=col(L,4,m->curR_), g=col(L,5,m->curG_), b=col(L,6,m->curB_);
    for (int dy=-rad; dy<=rad; ++dy) for (int dx=-rad; dx<=rad; ++dx)
      if (dx*dx+dy*dy <= rad*rad) put(cx+dx, cy+dy, r, g, b);
    return 0;
  }
  // Triangle outline: three segments through the vertices.
  static int l_triangle(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    int x0=(int)luaL_checkinteger(L,1), y0=(int)luaL_checkinteger(L,2),
        x1=(int)luaL_checkinteger(L,3), y1=(int)luaL_checkinteger(L,4),
        x2=(int)luaL_checkinteger(L,5), y2=(int)luaL_checkinteger(L,6);
    const int r=col(L,7,m->curR_), g=col(L,8,m->curG_), b=col(L,9,m->curB_);
    seg(x0,y0,x1,y1,r,g,b); seg(x1,y1,x2,y2,r,g,b); seg(x2,y2,x0,y0,r,g,b);
    return 0;
  }
  // Filled triangle: rasterise via edge functions over the clamped bounding
  // box. Accepts either winding (a pixel is inside when all three edge
  // functions share a sign), so callers needn't order the vertices.
  static int l_fill_triangle(lua_State *L) {
    LuaScriptMode *m = s_cur; if (!m) return 0;
    const int x0=(int)luaL_checkinteger(L,1), y0=(int)luaL_checkinteger(L,2),
              x1=(int)luaL_checkinteger(L,3), y1=(int)luaL_checkinteger(L,4),
              x2=(int)luaL_checkinteger(L,5), y2=(int)luaL_checkinteger(L,6);
    const int r=col(L,7,m->curR_), g=col(L,8,m->curG_), b=col(L,9,m->curB_);
    int minx = std::min(x0, std::min(x1, x2)), maxx = std::max(x0, std::max(x1, x2));
    int miny = std::min(y0, std::min(y1, y2)), maxy = std::max(y0, std::max(y1, y2));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > m->width_ - 1) maxx = m->width_ - 1;
    if (maxy > m->height_ - 1) maxy = m->height_ - 1;
    for (int y = miny; y <= maxy; ++y)
      for (int x = minx; x <= maxx; ++x) {
        const long long w0 = orient(x1,y1,x2,y2,x,y);
        const long long w1 = orient(x2,y2,x0,y0,x,y);
        const long long w2 = orient(x0,y0,x1,y1,x,y);
        if ((w0>=0 && w1>=0 && w2>=0) || (w0<=0 && w1<=0 && w2<=0)) put(x,y,r,g,b);
      }
    return 0;
  }
  static int l_text(lua_State *L) {
    int x=(int)luaL_checkinteger(L,1), y=(int)luaL_checkinteger(L,2);
    const char *s = luaL_checkstring(L,3);
    LuaScriptMode *m = s_cur; if (!m || !m->fontOK_) return 0;
    const int r=col(L,4,m->curR_), g=col(L,5,m->curG_), b=col(L,6,m->curB_);
    BufCanvas bc(m->buf_.data(), m->width_, m->height_);
    rgb_matrix::DrawText(&bc, m->font_, x, y + m->font_.baseline(), rgb_matrix::Color(r,g,b), s);
    return 0;
  }
  static int l_scrollh(lua_State *L) {
    int n = (int)luaL_checkinteger(L,1); LuaScriptMode *m = s_cur; if (!m || n==0) return 0;
    const int W=m->width_, H=m->height_; n = ((n % W) + W) % W;
    std::vector<uint8_t> row(W*3);
    for (int y=0; y<H; ++y) {
      uint8_t *r0 = &m->buf_[(size_t)y*W*3];
      for (int x=0; x<W; ++x) { const int sx=(x - n + W) % W; memcpy(&row[x*3], &r0[sx*3], 3); }
      memcpy(r0, row.data(), W*3);
    }
    return 0;
  }
  static int l_scrollv(lua_State *L) {
    int n = (int)luaL_checkinteger(L,1); LuaScriptMode *m = s_cur; if (!m || n==0) return 0;
    const int W=m->width_, H=m->height_; n = ((n % H) + H) % H;
    std::vector<uint8_t> tmp(m->buf_);
    for (int y=0; y<H; ++y) { const int sy=(y - n + H) % H; memcpy(&m->buf_[(size_t)y*W*3], &tmp[(size_t)sy*W*3], W*3); }
    return 0;
  }

  std::vector<uint8_t> buf_;
  lua_State *L_ = nullptr;
  std::mutex mtx_;
  std::mutex errMtx_;                              // guards err_ (render vs. web thread)
  std::string pending_, err_, status_ = "waiting for script...";
  std::string argHint_, argRaw_;                  // arg hint (arghint global) + raw KEY=VALUE
  bool hasPending_ = false, haveLoop_ = false;
  bool fontTried_ = false, fontOK_ = false;
  long frame_ = 0;
  int curR_ = 255, curG_ = 255, curB_ = 255;      // current pen colour (color())
  rgb_matrix::Font font_;
  static LuaScriptMode *s_cur;
};

#endif  // LED_MATRIX_SERVER_LUA_ENGINE_H
