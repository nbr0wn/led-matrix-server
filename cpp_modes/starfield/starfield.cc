// StarfieldMode -- registered as mode 4.
#include "common/mode.h"

// --- 3: Starfield ---------------------------------------------------------
// Warp starfield: stars rush out from the center toward the viewer, drawn as
// brightening streaks. Mirrors the Lua-mode default script (LuaMode::DefaultScript).
class StarfieldMode : public Mode {
public:
  const char *name() const override { return "Starfield"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    cx_ = w / 2.0f; cy_ = h / 2.0f;
    stars_.clear();
    for (int i = 0; i < N; ++i) stars_.push_back(NewStar(true));
  }
  void Draw(Canvas *c, float /*t*/, float /*dt*/) override {
    const float SPEED = 6.0f;               // depth units per frame (matches Lua)
    c->Fill(0, 0, 0);
    for (Star &s : stars_) {
      float pz = s.z;                        // previous depth, for the streak tail
      s.z -= SPEED;
      if (s.z <= 1.0f) { s = NewStar(false); pz = s.z + SPEED; }
      const float k = 100.0f / s.z, pk = 100.0f / pz;
      const int px = (int)floorf(cx_ + s.x * k),  py = (int)floorf(cy_ + s.y * k);
      const int qx = (int)floorf(cx_ + s.x * pk), qy = (int)floorf(cy_ + s.y * pk);
      float f = 1.0f - s.z / (float)width_;  // nearer => brighter
      if (f < 0.0f) f = 0.0f;
      DrawLine(c, qx, qy, px, py, (uint8_t)(s.r * f), (uint8_t)(s.g * f), (uint8_t)(s.b * f));
    }
  }
private:
  enum { N = 2000 };
  struct Star { float x, y, z; uint8_t r, g, b; };
  Star NewStar(bool far) {
    Star s;
    s.x = (frand() - 0.5f) * width_ * 2.0f;
    s.y = (frand() - 0.5f) * height_ * 2.0f;
    s.z = far ? frand() * width_ : (float)width_;   // far: spread in depth; else start at back
    s.r = (uint8_t)(160 + rand() % 96);
    s.g = (uint8_t)(160 + rand() % 96);
    s.b = (uint8_t)(200 + rand() % 56);             // lean slightly blue-white
    return s;
  }
  float cx_ = 0.0f, cy_ = 0.0f;
  std::vector<Star> stars_;
};

REGISTER_MODE(3, StarfieldMode);
