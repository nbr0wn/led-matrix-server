// FlowFieldMode -- registered as mode 16.
#include "common/mode.h"

// --- 17: Flow field particles ---------------------------------------------
class FlowFieldMode : public TrailMode {
public:
  const char *name() const override { return "Flow Field"; }
  void Activate(int w, int h) override {
    TrailMode::Activate(w, h);
    const int n = w * h / 30;
    px_.resize(n); py_.resize(n);
    for (int i = 0; i < n; ++i) { px_[i] = frand() * w; py_[i] = frand() * h; }
  }
  void Draw(Canvas *c, float t, float dt) override {
    Fade(236);
    for (size_t i = 0; i < px_.size(); ++i) {
      float x = px_[i], y = py_[i];
      const float a = (sinf(x * 0.03f) + cosf(y * 0.035f) +
                       sinf((x + y) * 0.02f + t)) * 2.0f;
      x += cosf(a) * 20 * dt; y += sinf(a) * 20 * dt;
      if (x < 0) x += width_;
      if (x >= width_) x -= width_;
      if (y < 0) y += height_;
      if (y >= height_) y -= height_;
      px_[i] = x; py_[i] = y;
      uint8_t r, g, b;
      HueToRGB(a * 0.15f + t * 0.05f, &r, &g, &b);
      AddPix((int)x, (int)y, r / 2, g / 2, b / 2);
    }
    Blit(c);
  }
private:
  std::vector<float> px_, py_;
};

REGISTER_MODE(15, FlowFieldMode);
