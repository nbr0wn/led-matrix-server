// ReactionDiffusionMode -- registered as mode 23.
#include "common/mode.h"

// --- 27: Reaction-Diffusion (Gray-Scott Turing patterns) ------------------
// Two virtual chemicals diffuse and react; tiny random seeds grow into
// coral/spots/worms. Random droplets are injected over time and the kill
// rate drifts slowly, so the pattern never settles.
class ReactionDiffusionMode : public Mode {
public:
  const char *name() const override { return "Reaction-Diffusion"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    u_.assign(w * h, 1.0f); v_.assign(w * h, 0.0f);
    un_.assign(w * h, 1.0f); vn_.assign(w * h, 0.0f);
    for (int i = 0; i < 25; ++i) SeedBlob();
    f_ = 0.037f;
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    if (rand() % 15 == 0) SeedBlob();
    const float Du = 0.16f, Dv = 0.08f;
    const float k = 0.06f + 0.003f * sinf(t * 0.05f);   // slow regime drift
    for (int s = 0; s < 3; ++s) {
      for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
          const int i = y * w + x;
          const float u = u_[i], vv = v_[i];
          const float lapU = u_[i-1] + u_[i+1] + u_[i-w] + u_[i+w] - 4 * u;
          const float lapV = v_[i-1] + v_[i+1] + v_[i-w] + v_[i+w] - 4 * vv;
          const float uvv = u * vv * vv;
          un_[i] = u + (Du * lapU - uvv + f_ * (1 - u));
          vn_[i] = vv + (Dv * lapV + uvv - (f_ + k) * vv);
        }
      u_.swap(un_); v_.swap(vn_);
    }
    int i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        const float val = clampi((int)(v_[i] * 500), 0, 255) / 255.0f;
        uint8_t r, g, b;
        HueToRGB(0.55f - val * 0.55f, &r, &g, &b);
        c->SetPixel(x, y, (uint8_t)(r * val), (uint8_t)(g * val), (uint8_t)(b * val));
      }
  }
private:
  void SeedBlob() {
    const int cx = 5 + rand() % (width_ - 10), cy = 5 + rand() % (height_ - 10);
    const int r = 3 + rand() % 4;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        const int x = cx + dx, y = cy + dy;
        if (x < 0 || x >= width_ || y < 0 || y >= height_) continue;
        if (dx * dx + dy * dy <= r * r) v_[y * width_ + x] = 1.0f;
      }
  }
  std::vector<float> u_, v_, un_, vn_;
  float f_;
};

REGISTER_MODE(22, ReactionDiffusionMode);
