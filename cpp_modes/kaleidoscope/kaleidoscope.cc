// KaleidoscopeMode -- registered as mode 20.
#include "common/mode.h"

// --- 23: Kaleidoscope (6-fold folded plasma) ------------------------------
class KaleidoscopeMode : public Mode {
public:
  const char *name() const override { return "Kaleidoscope"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    r_.resize(w * h); a_.resize(w * h);
    const float cx = w * 0.5f, cy = h * 0.5f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float dx = x - cx, dy = y - cy;
        r_[y * w + x] = sqrtf(dx * dx + dy * dy);
        a_[y * w + x] = atan2f(dy, dx);
      }
  }
  void Draw(Canvas *c, float t, float dt) override {
    const float seg = 6.2832f / 6.0f;
    int i = 0;
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x, ++i) {
        float ang = a_[i] + t * 0.2f;
        ang = fmodf(ang, seg); if (ang < 0) ang += seg;
        if (ang > seg * 0.5f) ang = seg - ang;  // mirror fold
        const float rr = r_[i];
        const float u = cosf(ang) * rr, v = sinf(ang) * rr;
        const float val = sinf(u * 0.15f + t) + sinf(v * 0.13f - t * 0.8f) +
                          sinf((u + v) * 0.1f + t * 0.5f);
        uint8_t r, g, b;
        HueToRGB((val + 3) / 6.0f + t * 0.05f, &r, &g, &b);
        c->SetPixel(x, y, r, g, b);
      }
  }
private:
  std::vector<float> r_, a_;
};

REGISTER_MODE(19, KaleidoscopeMode);
