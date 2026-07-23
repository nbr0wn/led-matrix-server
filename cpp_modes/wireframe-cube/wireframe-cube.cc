// WireframeCubeMode -- registered as mode 9.
#include "common/mode.h"

// --- 8: Rotating 3D wireframe cube ----------------------------------------
class WireframeCubeMode : public Mode {
public:
  const char *name() const override { return "Wireframe Cube"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    cubes_.clear();
    const int n = 1 + rand() % 5;                   // 1..5 cubes
    for (int i = 0; i < n; ++i) {
      Cube cu;
      cu.size = h * (0.12f + frand() * 0.20f);       // various sizes
      cu.cx = cu.size + frand() * fmaxf(1.0f, w - 2 * cu.size);   // keep the center on-screen
      cu.cy = cu.size + frand() * fmaxf(1.0f, h - 2 * cu.size);
      cu.sa = (0.3f + frand() * 1.0f) * (rand() % 2 ? 1.0f : -1.0f);   // random speeds + spin dir
      cu.sb = (0.3f + frand() * 1.0f) * (rand() % 2 ? 1.0f : -1.0f);
      cu.pa = frand() * 6.2832f;                     // random start angles
      cu.pb = frand() * 6.2832f;
      cu.hue = frand();
      cu.huerate = 0.05f + frand() * 0.15f;
      cubes_.push_back(cu);
    }
  }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    c->Fill(0, 0, 0);
    static const float V[8][3] = {
      {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
      {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}};
    static const int E[12][2] = {
      {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
      {0,4},{1,5},{2,6},{3,7}};
    for (const Cube &cu : cubes_) {
      const float sa = sinf(cu.pa + t * cu.sa), ca = cosf(cu.pa + t * cu.sa);
      const float sb = sinf(cu.pb + t * cu.sb), cb = cosf(cu.pb + t * cu.sb);
      int px[8], py[8];
      for (int i = 0; i < 8; ++i) {
        const float x = V[i][0], y = V[i][1], z = V[i][2];
        const float y1 = y * ca - z * sa, z1 = y * sa + z * ca;
        const float x1 = x * cb + z1 * sb, z2 = -x * sb + z1 * cb;
        const float p = 4.0f / (4.0f + z2);
        px[i] = (int)(cu.cx + x1 * cu.size * p);
        py[i] = (int)(cu.cy + y1 * cu.size * p);
      }
      uint8_t r, g, b;
      HueToRGB(cu.hue + t * cu.huerate, &r, &g, &b);
      for (int i = 0; i < 12; ++i)
        DrawLine(c, px[E[i][0]], py[E[i][0]], px[E[i][1]], py[E[i][1]], r, g, b);
    }
  }
private:
  struct Cube { float cx, cy, size, sa, sb, pa, pb, hue, huerate; };
  std::vector<Cube> cubes_;
};

REGISTER_MODE(8, WireframeCubeMode);
