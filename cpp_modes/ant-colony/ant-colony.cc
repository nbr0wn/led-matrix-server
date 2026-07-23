// AntColonyMode -- registered as mode 22.
#include "common/mode.h"

// --- 26: Ant colony (pheromone foraging) ----------------------------------
// Ants leave the nest, wander (randomly, biased by pheromone), and lay a
// "home" trail. On finding food they carry it back laying a "food" trail;
// others follow it. Trails evaporate. Food sources deplete and respawn
// elsewhere, so the trail network constantly reorganizes.
//
// Every ant also carries a goal, so none can spend forever circling a trail
// that has evaporated out from under it: a laden ant is steering for the queen
// at the nest (hard, when it can no longer smell a way home), and a forager
// that has hunted too long gives up on the pheromone and strikes out for the
// nearest food it can see. The queen banks what arrives and turns the surplus
// into brood, so the colony grows while it is fed and thins out when it isn't.
class AntColonyMode : public Mode {
public:
  const char *name() const override { return "Ant Colony"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    phFood_.assign(w * h, 0.0f);
    phHome_.assign(w * h, 0.0f);
    nestx_ = w / 2; nesty_ = h / 2;
    ants_.clear();
    for (int i = 0; i < 180; ++i) ants_.push_back(NewAnt());
    stores_ = 0.0f;
    sources_.clear();
    for (int i = 0; i < 4; ++i) { Food f; RespawnFood(f); sources_.push_back(f); }
  }
  void Draw(Canvas *c, float t, float dt) override {
    const int w = width_, h = height_;
    for (float &v : phFood_) v *= 0.985f;
    for (float &v : phHome_) v *= 0.985f;
    for (Ant &a : ants_) {
      std::vector<float> &field = a.food ? phHome_ : phFood_;
      const float SANG = 0.5f, TURN = 0.4f;
      const float fwd = Sense(field, a.x, a.y, a.ang);
      const float lft = Sense(field, a.x, a.y, a.ang - SANG);
      const float rgt = Sense(field, a.x, a.y, a.ang + SANG);
      if (fwd >= lft && fwd >= rgt) { /* stay the course */ }
      else if (lft > rgt) a.ang -= TURN;
      else a.ang += TURN;

      // Then override with the ant's goal. A laden ant is always headed for the
      // queen: it drifts along the home trail while it can smell one, and cuts
      // straight for her once the trail has evaporated. A forager that has
      // searched past its patience stops trusting the pheromone and beelines to
      // the nearest food. So no ant can end up circling a dead trail forever.
      float gx = 0.0f, gy = 0.0f;
      bool goal = false, hurry = false;
      if (a.food) {
        gx = (float)nestx_; gy = (float)nesty_; goal = true;
      } else if (a.lost > kPatience_) {
        const Food *f = NearestFood(a.x, a.y);
        if (f) { gx = f->x; gy = f->y; goal = hurry = true; }
      }
      if (goal) {
        const float trail = std::max(fwd, std::max(lft, rgt));
        const float pull = hurry ? 0.30f : (trail < 8.0f ? 0.34f : 0.11f);
        float d = atan2f(gy - a.y, gx - a.x) - a.ang;
        while (d >  3.1416f) d -= 6.2832f;
        while (d < -3.1416f) d += 6.2832f;
        a.ang += d * pull;
        a.ang += (frand() - 0.5f) * 0.18f;           // a little jitter, still
      } else {
        a.ang += (frand() - 0.5f) * 0.6f;            // free wander
      }
      a.lost = a.food ? 0.0f : a.lost + 1.0f;
      a.x += cosf(a.ang) * 1.2f;
      a.y += sinf(a.ang) * 1.2f;
      if (a.x < 1) { a.x = 1; a.ang = 3.1416f - a.ang; }
      if (a.x > w - 2) { a.x = w - 2; a.ang = 3.1416f - a.ang; }
      if (a.y < 1) { a.y = 1; a.ang = -a.ang; }
      if (a.y > h - 2) { a.y = h - 2; a.ang = -a.ang; }
      const int idx = (int)a.y * w + (int)a.x;
      if (a.food) phFood_[idx] = std::min(255.0f, phFood_[idx] + 40.0f);
      else        phHome_[idx] = std::min(255.0f, phHome_[idx] + 40.0f);
      if (!a.food) {
        for (Food &f : sources_) {
          const float dx = a.x - f.x, dy = a.y - f.y;
          const float fr = FoodRadius(f);
          if (f.amount > 0 && dx * dx + dy * dy < fr * fr) {
            a.food = true; f.amount -= 1; a.ang += 3.1416f;
            if (f.amount <= 0) RespawnFood(f);
            break;
          }
        }
      } else {
        const float dx = a.x - nestx_, dy = a.y - nesty_;
        if (dx * dx + dy * dy < 25) {                 // handed over to the queen
          a.food = false; a.ang += 3.1416f; a.lost = 0.0f;
          stores_ = std::min(400.0f, stores_ + 1.0f);
        }
      }
    }

    // The queen eats, and turns the surplus into brood. A colony whose foragers
    // keep finding food grows; one that goes hungry slowly thins back out.
    stores_ -= ants_.size() * 0.0038f;
    if (stores_ < 0.0f) stores_ = 0.0f;
    if (stores_ > 70.0f && ants_.size() < 300) { ants_.push_back(NewAnt()); stores_ -= 60.0f; }
    else if (stores_ <= 0.0f && ants_.size() > 90 && (rand() % 200) == 0) ants_.pop_back();
    int i = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x, ++i) {
        const int g = (int)std::min(255.0f, phFood_[i]);
        const int b = (int)std::min(255.0f, phHome_[i] * 0.7f);
        c->SetPixel(x, y, 0, (uint8_t)g, (uint8_t)b);
      }
    for (Food &f : sources_) {
      if (f.amount <= 0) continue;
      const float rad = FoodRadius(f);
      const int rr = (int)ceilf(rad);
      for (int dy = -rr; dy <= rr; ++dy)
        for (int dx = -rr; dx <= rr; ++dx) {
          const float d = sqrtf((float)(dx * dx + dy * dy));
          if (d > rad) continue;
          const float e = std::min(1.0f, rad - d);   // soft rim, so it melts away
          c->SetPixel((int)f.x + dx, (int)f.y + dy,
                      (uint8_t)(230 * e), (uint8_t)(200 * e), (uint8_t)(40 * e));
        }
    }
    DrawNest(c, t);
    for (Ant &a : ants_)
      c->SetPixel((int)a.x, (int)a.y,
                  a.food ? 255 : 220, a.food ? 240 : 60, a.food ? 120 : 40);
  }
private:
  struct Ant { float x, y, ang; bool food; float lost; };
  struct Food { float x, y, r, amount, cap; };
  // A pile shrinks as it is carried off: area tracks what is left, so the
  // radius follows the square root. Floored, so the last crumbs stay findable.
  static float FoodRadius(const Food &f) {
    return std::max(1.2f, f.r * sqrtf(std::max(0.0f, f.amount) / f.cap));
  }
  const float kPatience_ = 420.0f;    // frames a forager trusts the pheromone

  Ant NewAnt() {
    Ant a;
    a.x = (float)nestx_; a.y = (float)nesty_;
    a.ang = frand() * 6.2832f; a.food = false; a.lost = 0.0f;
    return a;
  }
  const Food *NearestFood(float x, float y) const {
    const Food *best = nullptr;
    float bd = 1e18f;
    for (const Food &f : sources_) {
      if (f.amount <= 0) continue;
      const float dx = f.x - x, dy = f.y - y, d = dx * dx + dy * dy;
      if (d < bd) { bd = d; best = &f; }
    }
    return best;
  }
  // The nest mound, the larder the foragers keep filling, and the queen: a fat
  // gold abdomen that pulses as she lays.
  void DrawNest(Canvas *c, float t) {
    for (int dy = -5; dy <= 5; ++dy)
      for (int dx = -5; dx <= 5; ++dx) {
        const int r2 = dx * dx + dy * dy;
        if (r2 <= 25)
          c->SetPixel(nestx_ + dx, nesty_ + dy, r2 > 12 ? 74 : 34, r2 > 12 ? 60 : 27,
                      r2 > 12 ? 48 : 21);                        // mound, lit rim
      }
    if (stores_ > 4.0f) {                                        // the larder
      const int pile = std::max(1, (int)std::min(4.0f, stores_ / 22.0f));
      for (int dy = -pile; dy <= pile; ++dy)
        for (int dx = -pile; dx <= pile; ++dx)
          if (dx * dx + dy * dy <= pile * pile)
            c->SetPixel(nestx_ + 9 + dx, nesty_ + 4 + dy, 235, 205, 45);
    }
    const int q = (int)(205 + 50 * sinf(t * 2.0f));              // she pulses, laying
    c->SetPixel(nestx_ - 1, nesty_ - 6, 225, 215, 185);          // antennae
    c->SetPixel(nestx_ + 1, nesty_ - 6, 225, 215, 185);
    c->SetPixel(nestx_, nesty_ - 5, 255, 250, 225);              // head
    c->SetPixel(nestx_ - 2, nesty_ - 3, 225, 215, 185);          // legs
    c->SetPixel(nestx_ + 2, nesty_ - 3, 225, 215, 185);
    c->SetPixel(nestx_ - 2, nesty_ - 1, 225, 215, 185);
    c->SetPixel(nestx_ + 2, nesty_ - 1, 225, 215, 185);
    c->SetPixel(nestx_, nesty_ - 4, 120, 96, 60);                // waist
    c->SetPixel(nestx_, nesty_ - 3, 252, 238, 200);              // thorax
    for (int dy = -1; dy <= 3; ++dy)                             // gravid abdomen
      for (int dx = -1; dx <= 1; ++dx)
        if (!(dx != 0 && dy >= 2)) c->SetPixel(nestx_ + dx, nesty_ + dy, q, q * 4 / 5, 60);
  }
  float Sense(const std::vector<float> &f, float x, float y, float ang) {
    const int sx = (int)(x + cosf(ang) * 5.0f), sy = (int)(y + sinf(ang) * 5.0f);
    if (sx < 0 || sx >= width_ || sy < 0 || sy >= height_) return 0.0f;
    return f[sy * width_ + sx];
  }
  void RespawnFood(Food &f) {
    do {
      f.x = 10 + frand() * (width_ - 20);
      f.y = 10 + frand() * (height_ - 20);
    } while ((f.x - nestx_) * (f.x - nestx_) +
             (f.y - nesty_) * (f.y - nesty_) < 900);
    f.r = 4 + frand() * 3;
    f.amount = 250 + frand() * 250;
    f.cap = f.amount;
  }
  std::vector<Ant> ants_;
  std::vector<float> phFood_, phHome_;
  std::vector<Food> sources_;
  int nestx_, nesty_;
  float stores_ = 0.0f;      // food banked at the queen
};

REGISTER_MODE(21, AntColonyMode);
