// LangtonsAntMode -- registered as mode 18.
#include "common/mode.h"

// --- 19: Langton's Ant ----------------------------------------------------
class LangtonsAntMode : public Mode {
public:
  const char *name() const override { return "Langton's Ant"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    grid_.assign(w * h, 0);
    age_.assign(w * h, 0);
    ax_ = w / 2; ay_ = h / 2; dir_ = rand() & 3; steps_ = 0;
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    for (int s = 0; s < 400; ++s) {
      const int idx = ay_ * w + ax_;
      if (grid_[idx]) { dir_ = (dir_ + 1) & 3; grid_[idx] = 0; }
      else            { dir_ = (dir_ + 3) & 3; grid_[idx] = 1; }
      if (age_[idx] < 255) ++age_[idx];
      ax_ = (ax_ + dx[dir_] + w) % w;
      ay_ = (ay_ + dy[dir_] + h) % h;
    }
    int i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        if (age_[i]) {
          uint8_t r, g, b;
          HueToRGB(0.5f + age_[i] * 0.02f, &r, &g, &b);
          const float f = grid_[i] ? 1.0f : 0.25f;
          c->SetPixel(x, y, (uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
        } else {
          c->SetPixel(x, y, 0, 0, 0);
        }
      }
    c->SetPixel(ax_, ay_, 255, 255, 255);
    if (++steps_ > 4000) Activate(w, h);
  }
private:
  std::vector<uint8_t> grid_, age_;
  int ax_, ay_, dir_, steps_;
};

REGISTER_MODE(17, LangtonsAntMode);
