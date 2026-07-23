// MatrixRainMode -- registered as mode 6.
#include "common/mode.h"

// --- 5: Matrix rain -------------------------------------------------------
class MatrixRainMode : public Mode {
public:
  const char *name() const override { return "Matrix Rain"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    head_.assign(w, 0);
    speed_.assign(w, 0);
    len_.assign(w, 0);
    for (int x = 0; x < w; ++x) Reset(x, true);
  }
  void Draw(Canvas *c, float /*t*/, float dt) override {
    c->Fill(0, 0, 0);
    for (int x = 0; x < width_; ++x) {
      head_[x] += speed_[x] * dt;
      const int head = (int)head_[x];
      for (int i = 0; i < len_[x]; ++i) {
        const int y = head - i;
        if (y < 0 || y >= height_) continue;
        if (i == 0) {
          c->SetPixel(x, y, 200, 255, 200);          // bright white-green head
        } else {
          const uint8_t g = (uint8_t)(255 * (len_[x] - i) / len_[x]);
          c->SetPixel(x, y, 0, g, 0);                 // fading green tail
        }
      }
      if (head - len_[x] > height_) Reset(x, false);
    }
  }
private:
  void Reset(int x, bool anywhere) {
    head_[x] = anywhere ? (float)(rand() % height_) : (float)(-(rand() % height_));
    speed_[x] = 20.0f + rand() % 60;
    len_[x] = 4 + rand() % 12;
  }
  std::vector<float> head_, speed_;
  std::vector<int> len_;
};

REGISTER_MODE(5, MatrixRainMode);
