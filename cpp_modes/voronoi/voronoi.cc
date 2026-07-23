// VoronoiMode -- registered as mode 19.
#include "common/mode.h"

// --- 20: Animated Voronoi cells -------------------------------------------
class VoronoiMode : public Mode {
public:
  const char *name() const override { return "Voronoi"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    const int n = 14;
    sx_.resize(n); sy_.resize(n); hue_.resize(n); phx_.resize(n); phy_.resize(n);
    for (int i = 0; i < n; ++i) {
      hue_[i] = frand(); phx_[i] = frand() * 6.2832f; phy_[i] = frand() * 6.2832f;
    }
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int n = (int)sx_.size();
    for (int i = 0; i < n; ++i) {
      sx_[i] = width_ * 0.5f + cosf(t * 0.3f + phx_[i]) * width_ * 0.4f;
      sy_[i] = height_ * 0.5f + sinf(t * 0.27f + phy_[i]) * height_ * 0.4f;
    }
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x) {
        float best = 1e9f, best2 = 1e9f;
        int bi = 0;
        for (int i = 0; i < n; ++i) {
          const float dx = x - sx_[i], dy = y - sy_[i];
          const float d = dx * dx + dy * dy;
          if (d < best) { best2 = best; best = d; bi = i; }
          else if (d < best2) best2 = d;
        }
        const float edge = sqrtf(best2) - sqrtf(best);
        const float bb = clampi((int)(edge * 12), 20, 255) / 255.0f;
        uint8_t r, g, bl;
        HueToRGB(hue_[bi] + t * 0.03f, &r, &g, &bl);
        c->SetPixel(x, y, (uint8_t)(r * bb), (uint8_t)(g * bb), (uint8_t)(bl * bb));
      }
  }
private:
  std::vector<float> sx_, sy_, hue_, phx_, phy_;
};

REGISTER_MODE(18, VoronoiMode);
