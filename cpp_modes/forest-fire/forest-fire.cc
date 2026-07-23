// ForestFireMode -- registered as mode 24.
#include "common/mode.h"

// --- 28: Forest fire (self-organized criticality) -------------------------
// Trees grow on empty ground at random; lightning randomly ignites a tree;
// fire spreads to neighbors and burns out to ash, which regrows. The result
// is endless waves of fire sweeping across a regrowing forest.
class ForestFireMode : public Mode {
public:
  const char *name() const override { return "Forest Fire"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    g_.assign(w * h, 0); n_.assign(w * h, 0);
    for (int i = 0, e = w * h; i < e; ++i) g_[i] = (rand() % 100 < 40) ? 1 : 0;
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    const float p = 0.008f;    // growth probability
    const float f = 0.00002f;  // lightning probability
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const int i = y * w + x;
        const uint8_t cur = g_[i];
        if (cur == 0) {
          n_[i] = (frand() < p) ? 1 : 0;
        } else if (cur == 1) {
          bool burn = false;
          for (int dy = -1; dy <= 1 && !burn; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              if (!dx && !dy) continue;
              const int nx = x + dx, ny = y + dy;
              if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
              if (g_[ny * w + nx] >= 2) { burn = true; break; }
            }
          n_[i] = (burn || frand() < f) ? 6 : 1;
        } else {
          n_[i] = (cur > 2) ? (uint8_t)(cur - 1) : 0;
        }
      }
    g_.swap(n_);
    int i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        const uint8_t s = g_[i];
        if (s == 0) {
          c->SetPixel(x, y, 12, 8, 4);
        } else if (s == 1) {
          const int tint = (x * 7 + y * 13) % 40;
          c->SetPixel(x, y, 10, (uint8_t)(90 + tint), 20);
        } else {
          const float b = (s - 1) / 5.0f;
          c->SetPixel(x, y, 255, (uint8_t)(90 + b * 150), (uint8_t)(b * 40));
        }
      }
  }
private:
  std::vector<uint8_t> g_, n_;
};

REGISTER_MODE(23, ForestFireMode);
