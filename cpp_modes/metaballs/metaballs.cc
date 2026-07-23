// MetaballsMode -- registered as mode 11.
#include "common/mode.h"

// --- 10: Metaballs --------------------------------------------------------
class MetaballsMode : public Mode {
public:
  const char *name() const override { return "Metaballs"; }
  void Draw(Canvas *c, float t, float dt) override {
    const int n = 6;
    float bx[n], by[n];
    for (int i = 0; i < n; ++i) {
      bx[i] = width_ * 0.5f + cosf(t * (0.5f + 0.13f * i) + i) * width_ * 0.35f;
      by[i] = height_ * 0.5f + sinf(t * (0.4f + 0.11f * i) + i * 2) * height_ * 0.35f;
    }
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x) {
        float s = 0;
        for (int i = 0; i < n; ++i) {
          const float dx = x - bx[i], dy = y - by[i];
          s += 600.0f / (dx * dx + dy * dy + 1.0f);
        }
        if (s < 1.0f) {
          c->SetPixel(x, y, 0, 0, (uint8_t)(s * 30));
        } else {
          uint8_t r, g, b;
          HueToRGB(s * 0.05f + t * 0.1f, &r, &g, &b);
          c->SetPixel(x, y, r, g, b);
        }
      }
  }
};

REGISTER_MODE(10, MetaballsMode);
