// DLAMode -- registered as mode 26.
#include "common/mode.h"

// --- 30: Diffusion-Limited Aggregation (crystal growth) -------------------
// Particles released near the cluster random-walk until they touch it, then
// stick. The frozen dendrite grows outward in fractal branches, colored by
// the time each particle attached. When it fills the screen it dissolves and
// regrows from a fresh seed.
class DLAMode : public Mode {
public:
  const char *name() const override { return "Crystal (DLA)"; }
  void Activate(int w, int h) override { Mode::Activate(w, h); Reset(); }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    for (int n = 0; n < 25; ++n) {
      const float a = frand() * 6.2832f;
      int x = clampi(cx_ + (int)(cosf(a) * birthR_), 1, w - 2);
      int y = clampi(cy_ + (int)(sinf(a) * birthR_), 1, h - 2);
      const int maxsteps = (int)(birthR_ * 10) + 200;
      for (int s = 0; s < maxsteps; ++s) {
        x += (rand() % 3) - 1; y += (rand() % 3) - 1;
        if (x < 1 || x >= w - 1 || y < 1 || y >= h - 1) break;
        if (grid_[y*w + x-1] || grid_[y*w + x+1] ||
            grid_[(y-1)*w + x] || grid_[(y+1)*w + x]) {
          grid_[y * w + x] = (uint8_t)(1 + (age_ & 63));
          const float d = sqrtf((float)((x-cx_)*(x-cx_) + (y-cy_)*(y-cy_)));
          const float cap = std::min(w, h) / 2.0f - 2.0f;
          if (d + 3 > birthR_) birthR_ = std::min(cap, d + 6);
          ++count_;
          break;
        }
      }
    }
    ++age_;
    if (fade_ == 0 && (birthR_ >= std::min(w, h) / 2.0f - 3.0f ||
                       count_ > w * h / 6)) {
      fade_ = 45;
    }
    int i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        if (grid_[i]) {
          uint8_t r, g, b;
          HueToRGB((grid_[i] - 1) / 64.0f, &r, &g, &b);
          c->SetPixel(x, y, r, g, b);
        } else {
          c->SetPixel(x, y, 0, 0, 0);
        }
      }
    if (fade_ > 0 && --fade_ == 0) Reset();
  }
private:
  void Reset() {
    grid_.assign(width_ * height_, 0);
    cx_ = width_ / 2; cy_ = height_ / 2;
    grid_[cy_ * width_ + cx_] = 1;
    birthR_ = 5.0f; count_ = 0; age_ = 0; fade_ = 0;
  }
  std::vector<uint8_t> grid_;
  int cx_, cy_, count_, age_, fade_;
  float birthR_;
};

REGISTER_MODE(25, DLAMode);
