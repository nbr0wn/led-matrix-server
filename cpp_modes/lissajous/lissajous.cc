// LissajousMode -- registered as mode 14.
#include "common/mode.h"

// --- 14: Lissajous curves -------------------------------------------------
class LissajousMode : public TrailMode {
public:
  const char *name() const override { return "Lissajous"; }
  void Draw(Canvas *c, float t, float dt) override {
    Fade(232);
    const float a = 3 + 1.5f * sinf(t * 0.05f);
    const float b = 2 + 1.5f * cosf(t * 0.037f);
    for (int i = 0; i < 70; ++i) {
      const float p = t * 2 + i * 0.05f;
      const int x = (int)(width_ * 0.5f + sinf(a * p) * width_ * 0.45f);
      const int y = (int)(height_ * 0.5f + sinf(b * p + 1.0f) * height_ * 0.45f);
      uint8_t r, g, bl;
      HueToRGB(t * 0.1f + i * 0.01f, &r, &g, &bl);
      AddPix(x, y, r, g, bl);
    }
    Blit(c);
  }
};

REGISTER_MODE(13, LissajousMode);
