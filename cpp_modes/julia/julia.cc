// JuliaMode -- registered as mode 8.
#include "common/mode.h"

// --- 7: Animated Julia set ------------------------------------------------
class JuliaMode : public Mode {
public:
  const char *name() const override { return "Julia Set"; }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    const float cr = 0.7885f * cosf(t * 0.3f);
    const float ci = 0.7885f * sinf(t * 0.3f);
    const int maxit = 48;
    const float scale = 3.0f / h;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        float zx = (x - w * 0.5f) * scale;
        float zy = (y - h * 0.5f) * scale;
        int it = 0;
        while (it < maxit) {
          const float x2 = zx * zx, y2 = zy * zy;
          if (x2 + y2 > 4.0f) break;
          zy = 2 * zx * zy + ci;
          zx = x2 - y2 + cr;
          ++it;
        }
        if (it >= maxit) { c->SetPixel(x, y, 0, 0, 0); continue; }
        uint8_t r, g, b;
        HueToRGB((float)it / maxit + t * 0.1f, &r, &g, &b);
        // Black background: fast-escaping regions (low iteration) fade to black,
        // so only the filaments near the set boundary glow.
        const float f = (float)it / maxit;
        c->SetPixel(x, y, (uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
      }
    }
  }
};

REGISTER_MODE(7, JuliaMode);
