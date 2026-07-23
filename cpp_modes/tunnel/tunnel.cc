// TunnelMode -- registered as mode 10.
#include "common/mode.h"

// --- 9: Demoscene tunnel --------------------------------------------------
class TunnelMode : public Mode {
public:
  const char *name() const override { return "Tunnel"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    ang_.resize(w * h); depth_.resize(w * h); shade_.resize(w * h);
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float maxd = sqrtf(cx * cx + cy * cy);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = x - cx, dy = y - cy;
        const float d = sqrtf(dx * dx + dy * dy) + 0.0001f;
        const int i = y * w + x;
        ang_[i] = atan2f(dy, dx) / 6.2832f + 0.5f;
        depth_[i] = 32.0f / d;
        shade_[i] = d / maxd;
      }
  }
  void Draw(Canvas *c, float t, float dt) override {
    int i = 0;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, ++i) {
        const float u = ang_[i] * 24.0f + t * 0.6f;
        const float v = depth_[i] + t * 5.0f;
        const int chk = ((int)u & 1) ^ ((int)v & 1);
        const float bright = shade_[i] * (chk ? 1.0f : 0.3f);
        uint8_t r, g, b;
        HueToRGB(v * 0.03f + t * 0.08f, &r, &g, &b);
        c->SetPixel(x, y, (uint8_t)(r * bright), (uint8_t)(g * bright),
                    (uint8_t)(b * bright));
      }
  }
private:
  std::vector<float> ang_, depth_, shade_;
};

REGISTER_MODE(9, TunnelMode);
