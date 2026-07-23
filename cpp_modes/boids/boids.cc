// BoidsMode -- registered as mode 17.
#include "common/mode.h"

// --- 18: Boids flocking ---------------------------------------------------
class BoidsMode : public Mode {
public:
  const char *name() const override { return "Boids"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    boids_.resize(60);
    for (B &b : boids_) {
      b.x = frand() * w; b.y = frand() * h;
      const float a = frand() * 6.2832f;
      b.vx = cosf(a) * 22; b.vy = sinf(a) * 22;
    }
  }
  void Draw(Canvas *c, float t, float dt) override {
    c->Fill(0, 0, 0);
    const float R2 = 14 * 14;
    for (size_t i = 0; i < boids_.size(); ++i) {
      float cx = 0, cy = 0, avx = 0, avy = 0, sx = 0, sy = 0;
      int cnt = 0;
      for (size_t j = 0; j < boids_.size(); ++j) {
        if (i == j) continue;
        const float dx = boids_[j].x - boids_[i].x, dy = boids_[j].y - boids_[i].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < R2 && d2 > 0) {
          cx += boids_[j].x; cy += boids_[j].y;
          avx += boids_[j].vx; avy += boids_[j].vy;
          sx -= dx / d2; sy -= dy / d2;
          ++cnt;
        }
      }
      B &b = boids_[i];
      if (cnt) {
        cx /= cnt; cy /= cnt; avx /= cnt; avy /= cnt;
        b.vx += (cx - b.x) * 0.02f + (avx - b.vx) * 0.05f + sx * 20.0f;
        b.vy += (cy - b.y) * 0.02f + (avy - b.vy) * 0.05f + sy * 20.0f;
      }
      float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
      if (sp < 1e-3f) sp = 1e-3f;
      const float target = sp > 38 ? 38 : (sp < 16 ? 16 : sp);
      b.vx = b.vx / sp * target; b.vy = b.vy / sp * target;
    }
    for (B &b : boids_) {
      b.x += b.vx * dt; b.y += b.vy * dt;
      if (b.x < 0) b.x += width_;
      if (b.x >= width_) b.x -= width_;
      if (b.y < 0) b.y += height_;
      if (b.y >= height_) b.y -= height_;
      uint8_t r, g, bl;
      HueToRGB(atan2f(b.vy, b.vx) / 6.2832f + 0.5f, &r, &g, &bl);
      c->SetPixel((int)b.x, (int)b.y, r, g, bl);
      c->SetPixel((int)(b.x - b.vx * 0.05f), (int)(b.y - b.vy * 0.05f),
                  r / 3, g / 3, bl / 3);
    }
  }
private:
  struct B { float x, y, vx, vy; };
  std::vector<B> boids_;
};

REGISTER_MODE(16, BoidsMode);
