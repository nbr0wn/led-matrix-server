// FireworksMode -- registered as mode 13.
#include "common/mode.h"

// --- 13: Fireworks --------------------------------------------------------
class FireworksMode : public TrailMode {
public:
  const char *name() const override { return "Fireworks"; }
  void Activate(int w, int h) override { TrailMode::Activate(w, h); parts_.clear(); timer_ = 0; }
  void Draw(Canvas *c, float t, float dt) override {
    Fade(210);
    if (--timer_ <= 0) { Launch(); timer_ = 15 + rand() % 30; }
    for (size_t i = 0; i < parts_.size();) {
      P &p = parts_[i];
      p.vy += 70.0f * dt;  // gravity
      p.x += p.vx * dt; p.y += p.vy * dt; p.life -= dt;
      if (p.rocket && p.vy >= 0) { Explode(p); p.life = 0; }
      if (p.life <= 0 || p.y >= height_) { parts_[i] = parts_.back(); parts_.pop_back(); continue; }
      const float f = std::min(1.0f, p.life);
      AddPix((int)p.x, (int)p.y, (int)(p.r * f), (int)(p.g * f), (int)(p.b * f));
      ++i;
    }
    Blit(c);
  }
private:
  struct P { float x, y, vx, vy, life; uint8_t r, g, b; bool rocket; };
  void Launch() {
    P p; p.x = rand() % width_; p.y = height_ - 1;
    p.vx = (rand() % 40 - 20); p.vy = -(75 + rand() % 35);
    p.life = 4; p.rocket = true; p.r = p.g = p.b = 255;
    parts_.push_back(p);
  }
  void Explode(P &src) {
    uint8_t r, g, b; HueToRGB(frand(), &r, &g, &b);
    const int n = 30 + rand() % 30;
    for (int i = 0; i < n; ++i) {
      P p; p.x = src.x; p.y = src.y;
      const float a = frand() * 6.2832f, sp = 15 + rand() % 45;
      p.vx = cosf(a) * sp; p.vy = sinf(a) * sp;
      p.life = 0.8f + frand() * 1.2f; p.rocket = false;
      p.r = r; p.g = g; p.b = b;
      parts_.push_back(p);
    }
  }
  std::vector<P> parts_;
  int timer_ = 0;
};

REGISTER_MODE(12, FireworksMode);
