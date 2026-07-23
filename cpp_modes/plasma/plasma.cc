// PlasmaMode -- registered as mode 1.
#include "common/mode.h"

// --- 0: Plasma ------------------------------------------------------------
class PlasmaMode : public Mode {
public:
  const char *name() const override { return "Plasma"; }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        float v = 0.0f;
        v += sinf((x * 0.11f) + t);
        v += sinf((y * 0.13f) - t * 0.9f);
        v += sinf((x * 0.07f + y * 0.09f) + t * 0.7f);
        const float cx = x * 0.5f - width_ * 0.25f;
        const float cy = y * 0.5f - height_ * 0.25f;
        v += sinf(sqrtf(cx * cx + cy * cy) * 0.15f + t);
        uint8_t r, g, b;
        HueToRGB((v + 4.0f) / 8.0f, &r, &g, &b);
        c->SetPixel(x, y, r, g, b);
      }
    }
  }
};

REGISTER_MODE(0, PlasmaMode);
