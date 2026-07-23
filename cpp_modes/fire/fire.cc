// FireMode -- registered as mode 5.
#include "common/mode.h"

// --- 4: Fire (Doom-style) -------------------------------------------------
class FireMode : public Mode {
public:
  const char *name() const override { return "Fire"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    heat_.assign(w * h, 0);
  }
  void Draw(Canvas *c, float /*t*/, float /*dt*/) override {
    const int w = width_, h = height_;
    for (int x = 0; x < w; ++x) heat_[(h - 1) * w + x] = 255;  // hot source row
    for (int y = h - 1; y >= 1; --y) {
      for (int x = 0; x < w; ++x) {
        const int decay = rand() % 3;
        int val = heat_[y * w + x] - decay * 12;
        if (val < 0) val = 0;
        const int destx = clampi(x + (decay ? (rand() % 3 - 1) : 0), 0, w - 1);
        heat_[(y - 1) * w + destx] = (uint8_t)val;
      }
    }
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int v = heat_[y * w + x];
        const uint8_t r = (uint8_t)std::min(255, v * 3);
        const uint8_t g = (uint8_t)(v < 85 ? 0 : std::min(255, (v - 85) * 3));
        const uint8_t b = (uint8_t)(v < 170 ? 0 : std::min(255, (v - 170) * 3));
        c->SetPixel(x, y, r, g, b);
      }
    }
  }
private:
  std::vector<uint8_t> heat_;
};

REGISTER_MODE(4, FireMode);
