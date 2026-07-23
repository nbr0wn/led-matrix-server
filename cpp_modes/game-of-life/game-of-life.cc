// GameOfLifeMode -- registered as mode 7.
#include "common/mode.h"

// --- 6: Conway's Game of Life (color-aged) --------------------------------
class GameOfLifeMode : public Mode {
public:
  const char *name() const override { return "Game of Life"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    cur_.assign(w * h, 0);
    nxt_.assign(w * h, 0);
    Seed();
    frames_ = 0;
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    int alive = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = (x + dx + w) % w, ny = (y + dy + h) % h;
            if (cur_[ny * w + nx]) ++n;
          }
        const uint8_t a = cur_[y * w + x];
        uint8_t na;
        if (a) na = (n == 2 || n == 3) ? (uint8_t)std::min(200, a + 1) : 0;
        else   na = (n == 3) ? 1 : 0;
        nxt_[y * w + x] = na;
        if (na) {
          ++alive;
          uint8_t r, g, b;
          HueToRGB(0.33f + na * 0.006f, &r, &g, &b);
          c->SetPixel(x, y, r, g, b);
        } else {
          c->SetPixel(x, y, 0, 0, 0);
        }
      }
    }
    cur_.swap(nxt_);
    if (++frames_ > 600 || alive < w * h / 200) { Seed(); frames_ = 0; }
  }
private:
  void Seed() { for (uint8_t &v : cur_) v = (rand() % 100 < 28) ? 1 : 0; }
  std::vector<uint8_t> cur_, nxt_;
  int frames_ = 0;
};

REGISTER_MODE(6, GameOfLifeMode);
