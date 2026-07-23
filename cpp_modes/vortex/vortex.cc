// VortexMode -- registered as mode 15.
#include "common/mode.h"

// --- 16: Hypnotic vortex --------------------------------------------------
class VortexMode : public Mode {
public:
  const char *name() const override { return "Vortex"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    ang_.resize(w * h); dist_.resize(w * h);
    const float cx = w * 0.5f, cy = h * 0.5f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = x - cx, dy = y - cy;
        ang_[y * w + x] = atan2f(dy, dx);
        dist_[y * w + x] = sqrtf(dx * dx + dy * dy);
      }
  }
  void Draw(Canvas *c, float t, float dt) override {
    int i = 0;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, ++i) {
        const float hue = ang_[i] / 6.2832f * 3.0f + dist_[i] * 0.05f - t * 0.3f;
        const float vv = 0.55f + 0.45f * sinf(dist_[i] * 0.2f - t * 2.5f);
        uint8_t r, g, b;
        HueToRGB(hue, &r, &g, &b);
        c->SetPixel(x, y, (uint8_t)(r * vv), (uint8_t)(g * vv), (uint8_t)(b * vv));
      }
  }
private:
  std::vector<float> ang_, dist_;
};

REGISTER_MODE(14, VortexMode);
