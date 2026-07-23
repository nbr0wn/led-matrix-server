// OrbitalColonyMode -- registered as mode 30.
#include "common/mode.h"

// --- Orbital Colony (space station sim) -----------------------------------
// A modular space station orbiting a planet: it slowly spins, grows by docking
// new modules (habitats with lit windows, sun-tracking solar wings, radiators,
// greenhouses, antennas), receives shuttles that fly in and dock, keeps
// satellites in orbit, and runs point-defense lasers on stray asteroids. Below,
// a rotating planet shows a day/night terminator with city lights and an
// atmosphere rim; a starfield twinkles behind it all.
class OrbitalColonyMode : public Mode {
public:
  const char *name() const override { return "Orbital Colony"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    scx_ = w * 0.5f; scy_ = h * 0.42f;
    mods_.clear();
    Module hub; hub.lx = hub.ly = 0; hub.type = 0; hub.parent = -1; hub.size = 3; hub.seed = Hash2(1, 1);
    mods_.push_back(hub);
    for (int i = 0; i < 5; ++i) AddModule();
    shuttles_.clear(); asteroids_.clear(); sats_.clear();
    for (int i = 0; i < 4; ++i) { Sat s; s.a = frand() * 6.28f; s.r = 34 + frand() * 22; s.sp = 0.01f + frand() * 0.02f; sats_.push_back(s); }
    stars_.clear();
    for (int i = 0; i < 90; ++i) { stars_.push_back(rand() % w); stars_.push_back(rand() % h); stars_.push_back(40 + rand() % 180); }
    grow_ = 200; spawnS_ = 120; spawnA_ = 150;
  }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) Px(c, x, y, 4, 5, 12);
    const float sun = t * 0.06f, sdx = cosf(sun), sdy = sinf(sun);
    DrawStars(c, t);
    DrawPlanet(c, t, sdx, sdy);
    if (--grow_ <= 0 && (int)mods_.size() < 26) { AddModule(); grow_ = 240 + rand() % 300; }
    const float ang = t * 0.05f;
    DrawStation(c, t, ang, sdx, sdy);
    if (--spawnS_ <= 0) { SpawnShuttle(); spawnS_ = 220 + rand() % 300; }
    UpdateShuttles(c, t);
    for (Sat &s : sats_) { s.a += s.sp; Px(c, (int)(scx_ + cosf(s.a) * s.r), (int)(scy_ + sinf(s.a) * s.r * 0.7f), ((int)(t * 6) & 1) ? 255 : 120, 200, 255); }
    if (--spawnA_ <= 0) { SpawnAsteroid(); spawnA_ = 160 + rand() % 260; }
    UpdateAsteroids(c);
  }
private:
  struct Module { float lx, ly; int type, parent; float size; uint32_t seed; };
  struct Shuttle { float x, y, tx, ty; int st; float timer; };
  struct Asteroid { float x, y, vx, vy; int sz, dead; };
  struct Sat { float a, r, sp; };
  float scx_, scy_;
  int grow_, spawnS_, spawnA_;
  std::vector<Module> mods_;
  std::vector<Shuttle> shuttles_;
  std::vector<Asteroid> asteroids_;
  std::vector<Sat> sats_;
  std::vector<int> stars_;

  void AddModule() {
    static const float DD[6][2] = { {6,0},{-6,0},{0,6},{0,-6},{5,5},{-5,-5} };
    const int pi = rand() % (int)mods_.size(), d = rand() % 6;
    Module m;
    m.lx = mods_[pi].lx + DD[d][0]; m.ly = mods_[pi].ly + DD[d][1];
    m.parent = pi; m.type = 1 + rand() % 5; m.size = 2 + rand() % 2;
    m.seed = Hash2((int)(m.lx * 7) + 50, (int)(m.ly * 7) + 80);
    mods_.push_back(m);
  }
  void worldPos(const Module &m, float ang, float &wx, float &wy) {
    const float ca = cosf(ang), sa = sinf(ang);
    wx = scx_ + m.lx * ca - m.ly * sa;
    wy = scy_ + m.lx * sa + m.ly * ca;
  }
  void DrawStation(Canvas *c, float t, float ang, float sdx, float sdy) {
    for (size_t i = 0; i < mods_.size(); ++i) {     // struts
      if (mods_[i].parent < 0) continue;
      float ax, ay, bx, by; worldPos(mods_[i], ang, ax, ay); worldPos(mods_[mods_[i].parent], ang, bx, by);
      DrawLine(c, (int)ax, (int)ay, (int)bx, (int)by, 120, 120, 135);
    }
    for (size_t i = 0; i < mods_.size(); ++i) {
      Module &m = mods_[i]; float wx, wy; worldPos(m, ang, wx, wy);
      const int x = (int)wx, y = (int)wy, s = (int)m.size;
      switch (m.type) {
      case 0: for (int dy = -s; dy <= s; ++dy) for (int dx = -s; dx <= s; ++dx) Px(c, x + dx, y + dy, 150, 150, 165); break;
      case 1: for (int dy = -s; dy <= s; ++dy) for (int dx = -s; dx <= s; ++dx) Px(c, x + dx, y + dy, 90, 95, 110);
              for (int dy = -s; dy <= s; dy += 2) for (int dx = -s; dx <= s; dx += 2)
                if ((Hash2(x * 3 + dx, (int)m.seed + dy) + (unsigned)(t * 2)) % 5 != 0) Px(c, x + dx, y + dy, 255, 220, 150);
              break;
      case 2: DrawSolar(c, x, y, sdx, sdy); break;
      case 3: for (int dx = -s; dx <= s; ++dx) { Px(c, x + dx, y - 1, 200, 60, 50); Px(c, x + dx, y + 1, 200, 60, 50); } break;
      case 4: for (int dy = -s; dy <= s; ++dy) for (int dx = -s; dx <= s; ++dx) Px(c, x + dx, y + dy, 40, 200, 90 + (int)(40 * sinf(t + x))); break;
      case 5: for (int dy = -s; dy <= s; ++dy) Px(c, x, y + dy, 200, 200, 210); Px(c, x + s, y - s, 230, 230, 255); break;
      }
      if ((i & 1) && ((int)(t * 4) & 1)) Px(c, x + s, y + s, 255, 40, 40);   // beacon
    }
  }
  void DrawSolar(Canvas *c, int x, int y, float sdx, float sdy) {
    const float px = -sdy, py = sdx;                // panel spans perpendicular to the sun
    for (int k = -4; k <= 4; ++k) {
      const bool grid = (k & 1);
      Px(c, x + (int)(px * k), y + (int)(py * k), grid ? 40 : 70, grid ? 70 : 110, grid ? 120 : 200);
      Px(c, x + (int)(px * k + sdx), y + (int)(py * k + sdy), 30, 50, 90);
    }
  }
  void DrawPlanet(Canvas *c, float t, float sdx, float sdy) {
    const float pcx = width_ * 0.5f, pcy = height_ + 40.0f, R = 120.0f;
    for (int y = std::max(0, (int)(pcy - R)); y < height_; ++y)
      for (int x = 0; x < width_; ++x) {
        const float ddx = x - pcx, ddy = y - pcy, d2 = ddx * ddx + ddy * ddy;
        if (d2 > R * R) continue;
        const float nx = ddx / R, ny = ddy / R;
        float lit = clampi((int)(((-(nx * sdx + ny * sdy)) * 0.5f + 0.5f) * 255), 0, 255) / 255.0f;
        const uint32_t hsh = Hash2((int)(x + t * 4) % 512, y);
        const bool land = (hsh % 100) < 45;
        int r, g, b;
        if (lit < 0.25f) { r = g = b = 6; if ((hsh % 37) == 0) { r = 255; g = 200; b = 120; } }   // night city lights
        else { r = (int)((land ? 70 : 30) * lit); g = (int)((land ? 110 : 60) * lit); b = (int)((land ? 60 : 150) * lit); }
        Px(c, x, y, r, g, b);
        if (d2 > (R - 2) * (R - 2)) Px(c, x, y, r / 2 + 40, g / 2 + 60, b / 2 + 120);   // atmosphere rim
      }
  }
  void SpawnShuttle() {
    Shuttle s; const int side = rand() % 4;
    s.x = side == 0 ? -6 : side == 1 ? width_ + 6 : rand() % width_;
    s.y = side == 2 ? -6 : side == 3 ? height_ + 6 : rand() % height_;
    s.tx = scx_ + (frand() - 0.5f) * 30; s.ty = scy_ + (frand() - 0.5f) * 30;
    s.st = 0; s.timer = 0; shuttles_.push_back(s);
  }
  void UpdateShuttles(Canvas *c, float t) {
    for (size_t i = 0; i < shuttles_.size(); ) {
      Shuttle &s = shuttles_[i];
      if (s.st == 0) { s.x += (s.tx - s.x) * 0.02f; s.y += (s.ty - s.y) * 0.02f;
        if (fabsf(s.x - s.tx) < 1 && fabsf(s.y - s.ty) < 1) { s.st = 1; s.timer = 120 + rand() % 120; if (rand() % 2 && (int)mods_.size() < 26) AddModule(); } }
      else if (s.st == 1) { if (--s.timer <= 0) { s.st = 2; s.tx = (rand() % 2) ? -10 : width_ + 10; s.ty = rand() % height_; } }
      else { s.x += (s.tx - s.x) * 0.02f; s.y += (s.ty - s.y) * 0.02f;
        if (s.x < -8 || s.x > width_ + 8) { shuttles_.erase(shuttles_.begin() + i); continue; } }
      const int x = (int)s.x, y = (int)s.y;
      Px(c, x, y, 210, 215, 235); Px(c, x + 1, y, 210, 215, 235);
      if ((int)(t * 8) & 1) Px(c, x - 1, y, 255, 120, 0);   // thruster
      ++i;
    }
  }
  void SpawnAsteroid() {
    Asteroid a; a.x = (rand() % 2) ? -6 : width_ + 6; a.y = rand() % height_;
    a.vx = (a.x < 0 ? 1 : -1) * (0.3f + frand() * 0.5f); a.vy = (frand() - 0.5f) * 0.4f;
    a.sz = 1 + rand() % 2; a.dead = 0; asteroids_.push_back(a);
  }
  void UpdateAsteroids(Canvas *c) {
    for (size_t i = 0; i < asteroids_.size(); ) {
      Asteroid &a = asteroids_[i];
      if (a.dead > 0) {
        for (int k = 0; k < 6; ++k) Px(c, (int)a.x + rand() % 7 - 3, (int)a.y + rand() % 7 - 3, 255, 180, 60);
        if (--a.dead <= 0) { asteroids_.erase(asteroids_.begin() + i); continue; }
        ++i; continue;
      }
      a.x += a.vx; a.y += a.vy;
      const float dx = a.x - scx_, dy = a.y - scy_;
      if (dx * dx + dy * dy < 30 * 30) { DrawLine(c, (int)scx_, (int)scy_, (int)a.x, (int)a.y, 120, 255, 180); a.dead = 6; ++i; continue; }
      if (a.x < -8 || a.x > width_ + 8) { asteroids_.erase(asteroids_.begin() + i); continue; }
      for (int dy2 = -a.sz; dy2 <= a.sz; ++dy2) for (int dx2 = -a.sz; dx2 <= a.sz; ++dx2) Px(c, (int)a.x + dx2, (int)a.y + dy2, 110, 95, 80);
      ++i;
    }
  }
  void DrawStars(Canvas *c, float t) {
    for (size_t i = 0; i + 2 < stars_.size(); i += 3) {
      const int x = stars_[i], y = stars_[i + 1], b = stars_[i + 2];
      const bool tw = ((Hash2(x, y) + (unsigned)(t * 3)) % 23) == 0;
      Px(c, x, y, tw ? 255 : b, tw ? 255 : b, tw ? 255 : b);
    }
  }
};

REGISTER_MODE(29, OrbitalColonyMode);
