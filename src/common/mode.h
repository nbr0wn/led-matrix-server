// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// The surface every display mode compiles against: the Mode base class, the
// handful of shared drawing helpers, and the registry a mode uses to publish
// itself to the app. A mode is a self-contained translation unit under
// src/modes/<name>/ that includes this header and ends with REGISTER_MODE().
//
// This code is public domain
// (but note that the led-matrix library this depends on is GPL v2).
#ifndef LED_MATRIX_SERVER_COMMON_MODE_H
#define LED_MATRIX_SERVER_COMMON_MODE_H

#include "led-matrix.h"
#include "graphics.h"

#include <algorithm>
#include <atomic>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using rgb_matrix::Canvas;

// --------------------------------------------------------------------------
// Process-wide state the handful of "live data" modes need. Defined in
// led-matrix-server.cc; everything else here is self-contained.
// --------------------------------------------------------------------------
extern volatile bool interrupt_received;
extern std::atomic<bool> g_running;      // cleared once shutdown starts
extern int g_video_w, g_video_h;         // video-source resolution (-W/-H)

// --------------------------------------------------------------------------
// Small color helpers.
// --------------------------------------------------------------------------
inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Map a hue-like value to an RGB rainbow color. `h` is wrapped into [0,1).
void HueToRGB(float h, uint8_t *r, uint8_t *g, uint8_t *b);

// Random float in [0, 1).
inline float frand() { return (float)rand() / ((float)RAND_MAX + 1.0f); }

// Deterministic 2D hash -> 32 bits. Stable per (x,y), for static textures.
inline uint32_t Hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// Bresenham line. Out-of-bounds pixels are harmlessly ignored by SetPixel.
void DrawLine(Canvas *c, int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b);

// --------------------------------------------------------------------------
// Mode framework. Each mode renders one frame given the simulated time `t`
// (seconds) and per-frame delta `dt` (seconds). Modes may hold state and
// reset it in Activate(). All mode methods run on the render thread only.
// --------------------------------------------------------------------------
class Mode {
public:
  virtual ~Mode() {}
  virtual const char *name() const = 0;
  virtual void Activate(int w, int h) { width_ = w; height_ = h; }
  // Called on the outgoing mode when the panel switches away, for modes holding
  // something external (a stream, a process) that should be released promptly.
  // Runs on the render thread, so it must not block.
  virtual void Deactivate() {}
  virtual void Draw(Canvas *c, float t, float dt) = 0;
  // Optional argument from the control server (e.g. a stock ticker symbol).
  virtual void SetArg(const std::string &) {}
  // Default arg applied when the auto-cycler (mode 0) runs; NULL if none.
  virtual const char *DefaultArg() const { return nullptr; }
  // Whether this mode takes an argument (gets its own persisted textbox in the
  // web UI), and a placeholder hint for that box.
  virtual bool TakesArg() const { return false; }
  virtual const char *ArgHint() const { return ""; }
  // One line of usage shown under the mode's textbox in the web UI. Spell out
  // the accepted forms; the hint above is only a placeholder.
  virtual const char *ArgUsage() const { return ""; }
  // Optional one-line runtime status (a script error, say) for the web UI.
  // Called off the web thread, so an implementation must be thread-safe.
  virtual std::string StatusLine() { return std::string(); }
  // The mode's editable source, if any (fetched Lua modules return their
  // script so the web UI's pen button can load it into the editor). "" = none.
  virtual std::string SourceText() { return std::string(); }
protected:
  int width_ = 0, height_ = 0;
  // Bounds-checked, colour-clamped pixel plot (convenience for the sim modes).
  inline void Px(Canvas *c, int x, int y, int r, int g, int b) {
    if ((unsigned)x < (unsigned)width_ && (unsigned)y < (unsigned)height_)
      c->SetPixel(x, y, (uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
  }
};

class TrailMode : public Mode {
public:
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    buf_.assign((size_t)w * h * 3, 0);
  }
protected:
  std::vector<uint8_t> buf_;
  void Fade(int num) {  // num in 0..255; 230 ~= *0.90 each frame
    for (uint8_t &v : buf_) v = (uint8_t)((v * num) >> 8);
  }
  inline void AddPix(int x, int y, int r, int g, int b) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    const size_t i = ((size_t)y * width_ + x) * 3;
    buf_[i]     = (uint8_t)std::min(255, buf_[i] + r);
    buf_[i + 1] = (uint8_t)std::min(255, buf_[i + 1] + g);
    buf_[i + 2] = (uint8_t)std::min(255, buf_[i + 2] + b);
  }
  void Blit(Canvas *c) {
    size_t i = 0;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, i += 3)
        c->SetPixel(x, y, buf_[i], buf_[i + 1], buf_[i + 2]);
  }
};

// --------------------------------------------------------------------------
// Mode registry. Each mode's translation unit registers itself at static-init
// time, so adding a mode means adding a directory -- no central list to edit.
// The explicit `order` is what fixes the menu numbering: static initializers
// across translation units run in an unspecified order, so the registry sorts
// by it rather than trusting the link order.
// --------------------------------------------------------------------------
typedef Mode *(*ModeFactory)();
void RegisterMode(int order, ModeFactory make);
std::vector<Mode *> BuildModes();        // one instance of every mode, in order

#define REGISTER_MODE(order, Class)                                   \
  static Mode *Class##_New() { return new Class(); }                  \
  static const struct Class##_Registrar {                             \
    Class##_Registrar() { RegisterMode((order), &Class##_New); }      \
  } Class##_registrar_instance

// --------------------------------------------------------------------------
// The web UI drives the Lua mode (pre-filling the script box, submitting a new
// script, reporting runtime errors) without needing its definition: the Lua
// mode publishes itself here in its constructor.
// --------------------------------------------------------------------------
class LuaHost {
public:
  virtual ~LuaHost() {}
  virtual std::string Submit(const std::string &src) = 0;   // syntax-check + queue
  virtual std::string Current() = 0;                        // last script submitted
  virtual std::string LastError() = 0;                      // runtime error, or ""
  virtual Mode *AsMode() = 0;                               // for identity compares
  static const char *DefaultScript();                       // the built-in example
};
extern LuaHost *g_lua_host;

#endif  // LED_MATRIX_SERVER_COMMON_MODE_H
