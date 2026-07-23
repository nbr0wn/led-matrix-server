// WaterRipplesMode -- registered as mode 12.
#include "common/mode.h"

// --- 11: Water ripples (height-field) -------------------------------------
class WaterRipplesMode : public Mode {
public:
  const char *name() const override { return "Water Ripples"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    a_.assign(w * h, 0);
    b_.assign(w * h, 0);
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    if (rand() % 3 == 0) {
      const int x = 1 + rand() % (w - 2), y = 1 + rand() % (h - 2);
      a_[y * w + x] = 400 + rand() % 300;
    }
    for (int y = 1; y < h - 1; ++y)
      for (int x = 1; x < w - 1; ++x) {
        const int i = y * w + x;
        int val = ((a_[i - 1] + a_[i + 1] + a_[i - w] + a_[i + w]) >> 1) - b_[i];
        val -= val >> 6;  // damping
        b_[i] = val;
      }
    a_.swap(b_);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        // Black background: still water reads as black; only the wave amplitude
        // (deviation from rest) lights up, tinted blue-white.
        const int m = clampi(abs(a_[y * w + x]) * 3 / 2, 0, 255);
        c->SetPixel(x, y, (uint8_t)(m / 3), (uint8_t)(m / 2), (uint8_t)m);
      }
  }
private:
  std::vector<int> a_, b_;
};

REGISTER_MODE(11, WaterRipplesMode);
