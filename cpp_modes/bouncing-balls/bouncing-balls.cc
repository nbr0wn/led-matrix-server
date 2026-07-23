// BouncingBallsMode -- registered as mode 3.
#include "common/mode.h"

// --- 2: Bouncing balls ----------------------------------------------------
class BouncingBallsMode : public Mode {
public:
  const char *name() const override { return "Bouncing Balls"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    balls_.clear();
    const int n = 8;
    for (int i = 0; i < n; ++i) {
      Ball ball;
      ball.x = rand() % w;
      ball.y = rand() % h;
      ball.vx = (rand() % 40 - 20) + (rand() % 2 ? 15 : -15);
      ball.vy = (rand() % 40 - 20) + (rand() % 2 ? 15 : -15);
      ball.radius = 3 + rand() % 4;
      HueToRGB((float)i / n, &ball.r, &ball.g, &ball.b);
      balls_.push_back(ball);
    }
  }
  void Draw(Canvas *c, float /*t*/, float dt) override {
    c->Fill(0, 0, 0);
    for (Ball &ball : balls_) {
      ball.x += ball.vx * dt;
      ball.y += ball.vy * dt;
      if (ball.x < ball.radius) { ball.x = ball.radius; ball.vx = -ball.vx; }
      if (ball.x > width_ - 1 - ball.radius) { ball.x = width_ - 1 - ball.radius; ball.vx = -ball.vx; }
      if (ball.y < ball.radius) { ball.y = ball.radius; ball.vy = -ball.vy; }
      if (ball.y > height_ - 1 - ball.radius) { ball.y = height_ - 1 - ball.radius; ball.vy = -ball.vy; }
      const int cx = (int)ball.x, cy = (int)ball.y, rr = (int)ball.radius;
      for (int dy = -rr; dy <= rr; ++dy) {
        for (int dx = -rr; dx <= rr; ++dx) {
          if (dx * dx + dy * dy > rr * rr) continue;
          const int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < width_ && py >= 0 && py < height_)
            c->SetPixel(px, py, ball.r, ball.g, ball.b);
        }
      }
    }
  }
private:
  struct Ball { float x, y, vx, vy, radius; uint8_t r, g, b; };
  std::vector<Ball> balls_;
};

REGISTER_MODE(2, BouncingBallsMode);
