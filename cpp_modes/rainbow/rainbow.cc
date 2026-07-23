// RainbowMode -- registered as mode 2.
#include "common/mode.h"

// --- 1: Rainbow -----------------------------------------------------------
class RainbowMode : public Mode {
public:
  const char *name() const override { return "Rainbow"; }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        uint8_t r, g, b;
        HueToRGB((x + y) * 0.01f + t * 0.2f, &r, &g, &b);
        c->SetPixel(x, y, r, g, b);
      }
    }
  }
};

REGISTER_MODE(1, RainbowMode);
