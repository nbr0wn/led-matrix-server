// WaTorMode -- registered as mode 25.
#include "common/mode.h"

// --- 29: Wa-Tor (predator/prey population dynamics) -----------------------
// Fish wander and breed; sharks hunt fish, breed, and starve if they don't
// eat. Populations swing in Lotka-Volterra waves. All movement, breeding,
// and hunting choices are random. If a species dies out it is reseeded.
class WaTorMode : public Mode {
public:
  const char *name() const override { return "Wa-Tor"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    type_.assign(w * h, 0); breed_.assign(w * h, 0);
    energy_.assign(w * h, 0); moved_.assign(w * h, 0);
    for (int i = 0, e = w * h; i < e; ++i) {
      const int r = rand() % 100;
      if (r < 28) { type_[i] = 1; breed_[i] = (uint8_t)(rand() % FISH_BREED); }
      else if (r < 34) {
        type_[i] = 2; breed_[i] = (uint8_t)(rand() % SHARK_BREED);
        energy_[i] = SHARK_ENERGY;
      }
    }
    tick_ = 0;
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    if ((tick_++ & 1) == 0) UpdateSim();   // step every other frame
    int fish = 0, shark = 0, i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        if (type_[i] == 1) { c->SetPixel(x, y, 30, 120, 230); ++fish; }
        else if (type_[i] == 2) {
          const float e = energy_[i] / (float)SHARK_ENERGY;
          const float b = 0.4f + 0.6f * (e > 1 ? 1.0f : e);
          c->SetPixel(x, y, (uint8_t)(255 * b), (uint8_t)(70 * b), (uint8_t)(30 * b));
          ++shark;
        } else {
          c->SetPixel(x, y, 0, 0, 16);
        }
      }
    if (shark == 0) Sprinkle(2, 40);
    if (fish == 0)  Sprinkle(1, 200);
  }
private:
  static const int FISH_BREED = 4, SHARK_BREED = 14, SHARK_ENERGY = 14, EAT = 9;
  int NB(int x, int y, int k, int &ox, int &oy) {
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    ox = (x + dx[k] + width_) % width_; oy = (y + dy[k] + height_) % height_;
    return oy * width_ + ox;
  }
  void UpdateSim() {
    const int w = width_, h = height_;
    std::fill(moved_.begin(), moved_.end(), 0);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const int i = y * w + x;
        if (moved_[i] || type_[i] == 0) continue;
        if (type_[i] == 1) StepFish(x, y, i);
        else StepShark(x, y, i);
      }
  }
  void StepFish(int x, int y, int i) {
    int emp[4], ne = 0, ox, oy;
    for (int k = 0; k < 4; ++k) {
      const int j = NB(x, y, k, ox, oy);
      if (type_[j] == 0 && !moved_[j]) emp[ne++] = j;
    }
    const int nb = (int)breed_[i] + 1;
    if (ne > 0) {
      const int j = emp[rand() % ne];
      const bool bred = nb >= FISH_BREED;
      type_[j] = 1; breed_[j] = bred ? 0 : (uint8_t)nb; moved_[j] = 1;
      if (bred) { type_[i] = 1; breed_[i] = 0; moved_[i] = 1; }
      else type_[i] = 0;
    } else {
      breed_[i] = (uint8_t)nb;
    }
  }
  void StepShark(int x, int y, int i) {
    int fishn[4], fn = 0, emp[4], en = 0, ox, oy;
    for (int k = 0; k < 4; ++k) {
      const int j = NB(x, y, k, ox, oy);
      if (moved_[j]) continue;
      if (type_[j] == 1) fishn[fn++] = j;
      else if (type_[j] == 0) emp[en++] = j;
    }
    int e = (int)energy_[i] - 1;
    if (e <= 0) { type_[i] = 0; return; }   // starved
    const int nb = (int)breed_[i] + 1;
    int j = -1; bool ate = false;
    if (fn > 0) { j = fishn[rand() % fn]; ate = true; }
    else if (en > 0) { j = emp[rand() % en]; }
    if (ate) e += EAT;
    if (j >= 0) {
      const bool bred = nb >= SHARK_BREED;
      type_[j] = 2; moved_[j] = 1;
      if (bred) {
        energy_[j] = (uint8_t)(e - e / 2); breed_[j] = 0;
        type_[i] = 2; energy_[i] = (uint8_t)(e / 2); breed_[i] = 0; moved_[i] = 1;
      } else {
        energy_[j] = (uint8_t)e; breed_[j] = (uint8_t)nb; type_[i] = 0;
      }
    } else {
      energy_[i] = (uint8_t)e; breed_[i] = (uint8_t)nb;
    }
  }
  void Sprinkle(int what, int n) {
    for (int c = 0; c < n; ++c) {
      const int idx = rand() % (width_ * height_);
      if (what == 2 && type_[idx] != 2) {
        type_[idx] = 2; energy_[idx] = SHARK_ENERGY; breed_[idx] = 0;
      } else if (what == 1 && type_[idx] == 0) {
        type_[idx] = 1; breed_[idx] = 0;
      }
    }
  }
  std::vector<uint8_t> type_, breed_, energy_, moved_;
  int tick_;
};

REGISTER_MODE(24, WaTorMode);
