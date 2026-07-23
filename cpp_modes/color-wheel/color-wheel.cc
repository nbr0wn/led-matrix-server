// ColorWheelMode -- registered as mode 21.
#include "common/mode.h"

// --- 24: Rotating color wheel with pulsing rings --------------------------
class ColorWheelMode : public Mode {
public:
  const char *name() const override { return "Color Wheel"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    r_.resize(w * h); a_.resize(w * h);
    const float cx = w * 0.5f, cy = h * 0.5f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = x - cx, dy = y - cy;
        r_[y * w + x] = sqrtf(dx * dx + dy * dy);
        a_[y * w + x] = atan2f(dy, dx) / 6.2832f + 0.5f;
      }
  }
  void Draw(Canvas *c, float t, float dt) override {
    int i = 0;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, ++i) {
        const float ring = 0.5f + 0.5f * sinf(r_[i] * 0.25f - t * 3.0f);
        uint8_t r, g, b;
        HueToRGB(a_[i] + t * 0.15f, &r, &g, &b);
        c->SetPixel(x, y, (uint8_t)(r * ring), (uint8_t)(g * ring),
                    (uint8_t)(b * ring));
      }
  }
private:
  std::vector<float> r_, a_;
};

REGISTER_MODE(20, ColorWheelMode);
