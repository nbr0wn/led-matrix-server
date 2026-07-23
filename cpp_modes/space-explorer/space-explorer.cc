// SpaceExplorerMode -- registered as mode 31.
#include "common/mode.h"

// --- 32: Space Explorer (autonomous solar-system sim) ---------------------
// An AI ship roams a large scrolling solar system: it visits planets to
// collect fuel and ore (ore = ammo for its guns), fights enemy ships that
// hunt it, and heads for the warp gate to jump to the next, harder system.
// If destroyed it respawns a fresh run. The camera follows the ship, windowed
// over a map much larger than the screen (like the scrolling dungeon).
class SpaceExplorerMode : public Mode {
public:
  const char *name() const override { return "Space Explorer"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    WW_ = 560; WH_ = 400;
    stars_.clear();
    for (int i = 0; i < 260; ++i)
      stars_.push_back({ (float)(rand()%WW_), (float)(rand()%WH_),
                         (uint8_t)(40 + rand()%160) });
    NewGame();
  }
  void Draw(Canvas *c, float t, float dt) override {
    if (dt > 0.05f) dt = 0.05f;
    if (deathT_ > 0) { deathT_ -= dt; Render(c); if (deathT_ <= 0) NewGame(); return; }
    Step(dt);
    Render(c);
  }
private:
  struct Ship { float x, y, vx, vy, ang, fuel, maxfuel, oreBuf; int hp, maxhp, ammo; };
  struct Star { float x, y; uint8_t b; };
  struct Planet { float x, y, r, cap; uint8_t cr, cg, cb; float fuel, ore; int guards, kind; bool ring; };
  struct Enemy { float x, y, vx, vy; int hp; float cd; int guard; };  // guard: planet idx or -1
  struct Bullet { float x, y, vx, vy, life; bool foe; };

  static float dist(float ax, float ay, float bx, float by) {
    const float dx = ax-bx, dy = ay-by; return sqrtf(dx*dx + dy*dy);
  }
  static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
  }
  static float AngTowards(float cur, float target, float maxstep) {
    float d = target - cur;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    if (d > maxstep) d = maxstep;
    if (d < -maxstep) d = -maxstep;
    return cur + d;
  }
  static float AngLerp(float a, float b, float f) {
    float d = b - a;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    return a + d * f;
  }
  static float angDiff(float a, float b) {
    float d = a - b;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    return d;
  }
  void NewWander() { wx_ = 30 + rand()%(WW_-60); wy_ = 30 + rand()%(WH_-60); }
  Planet *NearestPlanet(bool wantFuel, bool needUnguarded) {
    Planet *best = nullptr; float bd = 1e18f;
    for (Planet &p : planets_) {
      if ((wantFuel ? p.fuel : p.ore) <= 1) continue;
      if (needUnguarded && p.guards > 0) continue;   // can't mine a locked planet
      const float d = dist(p.x, p.y, ship_.x, ship_.y);
      if (d < bd) { bd = d; best = &p; }
    }
    return best;
  }
  Enemy *NearestEnemy() {
    Enemy *best = nullptr; float bd = 1e18f;
    for (Enemy &e : enemies_) {
      const float d = dist(e.x, e.y, ship_.x, ship_.y);
      if (d < bd) { bd = d; best = &e; }
    }
    return best;
  }

  void NewGame() {
    ship_.maxfuel = 100; ship_.fuel = 100;
    ship_.maxhp = 100; ship_.hp = 100;
    ship_.ammo = 12; ship_.oreBuf = 0;
    system_ = 0; deathT_ = 0;
    NextSystem();
  }
  void NextSystem() {
    ++system_;
    ship_.fuel = std::min(ship_.maxfuel, ship_.fuel + 40.0f);
    ship_.hp = std::min(ship_.maxhp, ship_.hp + 20);
    enemies_.clear(); bullets_.clear();
    warpFlash_ = 0.6f; findFlash_ = 0; gateFound_ = false;
    ship_.x = 40; ship_.y = WH_ * 0.5f; ship_.vx = ship_.vy = 0; ship_.ang = 0;
    planets_.clear();
    const int np = std::min(9, 4 + system_/2);
    for (int i = 0; i < np; ++i) {
      Planet p; int tries = 0;
      do { p.x = 40 + rand()%(WW_-80); p.y = 30 + rand()%(WH_-60); ++tries; }
      while (Near(p.x, p.y, 60) && tries < 30);
      p.r = 6 + rand()%9; p.guards = 0;
      // Each planet gives EITHER fuel (green) or bullets/ammo (amber). Guarantee
      // at least one of each (indices 0 and 1), randomize the rest.
      p.kind = (i == 0) ? 0 : (i == 1) ? 1 : rand()%2;
      if (p.kind == 0) {                              // fuel planet
        p.fuel = 100 + rand()%80; p.ore = 0;
        p.cr = 30 + rand()%40; p.cg = 150 + rand()%80; p.cb = 70 + rand()%60;
      } else {                                        // ammo planet
        p.fuel = 0; p.ore = 80 + rand()%60;
        p.cr = 200 + rand()%55; p.cg = 120 + rand()%60; p.cb = 30 + rand()%40;
      }
      p.cap = (p.kind == 0) ? p.fuel : p.ore;         // finite reservoir
      p.ring = (rand()%3 == 0);
      planets_.push_back(p);
    }
    // Guardians defend the frontier planets (index >= 2); the two starter planets
    // stay open so the ship always has a safe place to refuel and rearm.
    const int gper = 1 + (system_ >= 3 ? 1 : 0);
    for (int pi = 2; pi < (int)planets_.size(); ++pi) {
      Planet &p = planets_[pi];
      for (int k = 0; k < gper; ++k) {
        const float a = frand()*6.2831853f, rr = p.r + 12 + rand()%8;
        Enemy e; e.x = p.x + cosf(a)*rr; e.y = p.y + sinf(a)*rr;
        e.vx = e.vy = 0; e.hp = 1 + system_/3; e.cd = 1.0f + frand(); e.guard = pi;
        enemies_.push_back(e); p.guards++;
      }
    }
    // Hidden warp gate, placed far from the start; the ship must find it.
    do { gx_ = 40 + rand()%(WW_-80); gy_ = 40 + rand()%(WH_-80); }
    while (dist(gx_, gy_, 40, WH_*0.5f) < 220);
    NewWander();
    enemyT_ = 3.0f;
  }
  bool Near(float x, float y, float d) {
    if (dist(x, y, gx_, gy_) < d) return true;
    for (Planet &p : planets_) if (fabsf(p.x-x) + fabsf(p.y-y) < d) return true;
    return false;
  }

  void Step(float dt) {
    // TURNRATE is high so the ship can snap around to keep an orbiting enemy in
    // its (forward-only) firing cone instead of losing the circling duel.
    const float THRUST = 55, MAXSPD = 60, TURNRATE = 7.0f, BULLET_SPD = 110,
                ENEMY_SPD = 32, ENEMY_ACC = 45, ENEMY_BSPD = 70;
    Ship &s = ship_;
    // Pick a goal: refuel/restock at planets if low; head to the warp gate
    // ONCE FOUND; otherwise roam the map to explore and search for it.
    float tx, ty;
    // With no bullets we can't unlock guarded planets, so only head to open ones.
    Planet *fp = NearestPlanet(true, s.ammo == 0);   // nearest fuel planet
    Planet *ap = NearestPlanet(false, s.ammo == 0);  // nearest ammo planet
    if (s.fuel / s.maxfuel < 0.35f && fp) { tx = fp->x; ty = fp->y; }  // low fuel
    else if (s.ammo < 4 && ap) { tx = ap->x; ty = ap->y; }            // low bullets
    else if (gateFound_) { tx = gx_; ty = gy_; }
    else { if (dist(wx_, wy_, s.x, s.y) < 40) NewWander(); tx = wx_; ty = wy_; }
    float desired = atan2f(ty - s.y, tx - s.x);
    // Only pick a fight when we actually have bullets: with ammo, turn to FACE the
    // nearest enemy (forward-only gun); with none, FLEE it and go resupply.
    Enemy *ne = NearestEnemy();
    const float ed = ne ? dist(ne->x, ne->y, s.x, s.y) : 1e9f;
    if (s.ammo > 0 && ne && ed < 100) desired = atan2f(ne->y - s.y, ne->x - s.x);
    else if (s.ammo == 0 && ne && ed < 70) desired = atan2f(s.y - ne->y, s.x - ne->x);
    s.ang = AngTowards(s.ang, desired, TURNRATE * dt);
    const bool hasFuel = s.fuel > 0;
    if (hasFuel) {
      s.vx += cosf(s.ang) * THRUST * dt;
      s.vy += sinf(s.ang) * THRUST * dt;
      s.fuel -= THRUST * dt * 0.05f;
      if (s.fuel < 0) s.fuel = 0;
    }
    s.vx *= 0.99f; s.vy *= 0.99f;
    float sp = sqrtf(s.vx*s.vx + s.vy*s.vy);
    if (sp > MAXSPD) { s.vx = s.vx/sp*MAXSPD; s.vy = s.vy/sp*MAXSPD; }
    s.x += s.vx*dt; s.y += s.vy*dt;
    if (s.x < 4) { s.x = 4; s.vx *= -0.4f; }
    if (s.x > WW_-4) { s.x = WW_-4; s.vx *= -0.4f; }
    if (s.y < 4) { s.y = 4; s.vy *= -0.4f; }
    if (s.y > WH_-4) { s.y = WH_-4; s.vy *= -0.4f; }
    thrusting_ = hasFuel && sp > 2;
    // Collect fuel/ore from a planet we're hovering over (ore -> ammo).
    // A planet stays locked until its guardians are defeated.
    for (Planet &p : planets_) {
      if (p.guards > 0) continue;
      if (dist(p.x, p.y, s.x, s.y) < p.r + 10) {
        if (p.fuel > 0 && s.fuel < s.maxfuel) {
          const float g = std::min(p.fuel, 25*dt);
          s.fuel = std::min(s.maxfuel, s.fuel + g); p.fuel -= g;
        }
        if (p.ore > 0) {
          const float g = std::min(p.ore, 12*dt); p.ore -= g; s.oreBuf += g;
          while (s.oreBuf >= 4) { s.oreBuf -= 4; s.ammo++; }
        }
      }
    }
    // Forward-only gun: fire only when an enemy is inside the ship's heading cone.
    fireCd_ -= dt;
    if (ne && s.ammo > 0 && fireCd_ <= 0 && dist(ne->x, ne->y, s.x, s.y) < 100 &&
        fabsf(angDiff(atan2f(ne->y-s.y, ne->x-s.x), s.ang)) < 0.30f) {
      bullets_.push_back({ s.x, s.y, cosf(s.ang)*BULLET_SPD, sinf(s.ang)*BULLET_SPD, 2.0f, false });
      s.ammo--; fireCd_ = 0.3f;
    }
    // Spawn enemies over time (more, tougher, faster each system).
    enemyT_ -= dt;
    const int maxE = std::min(8, 2 + system_);
    if (enemyT_ <= 0 && (int)enemies_.size() < maxE) {
      Enemy e; const int edge = rand()%4;
      e.x = edge==0 ? 0 : (edge==1 ? WW_ : rand()%WW_);
      e.y = edge==2 ? 0 : (edge==3 ? WH_ : rand()%WH_);
      e.vx = e.vy = 0; e.hp = 1 + system_/3; e.cd = 1.0f + frand(); e.guard = -1;
      enemies_.push_back(e);
      enemyT_ = 3.5f - std::min(2.0f, system_*0.2f);
    }
    // Enemy AI: guardians hold near their planet until the ship comes close;
    // free-roamers always hunt the ship. All of them shoot when in range.
    for (Enemy &e : enemies_) {
      float txe = s.x, tye = s.y;
      if (e.guard >= 0 && e.guard < (int)planets_.size()) {
        const Planet &gp = planets_[e.guard];
        if (dist(s.x, s.y, gp.x, gp.y) > 120 && dist(e.x, e.y, s.x, s.y) > 55) {
          txe = gp.x; tye = gp.y;   // return to guard post
        }
      }
      const float a = atan2f(tye - e.y, txe - e.x);
      e.vx += cosf(a)*ENEMY_ACC*dt; e.vy += sinf(a)*ENEMY_ACC*dt;
      const float esp = sqrtf(e.vx*e.vx + e.vy*e.vy);
      if (esp > ENEMY_SPD) { e.vx = e.vx/esp*ENEMY_SPD; e.vy = e.vy/esp*ENEMY_SPD; }
      e.x += e.vx*dt; e.y += e.vy*dt;
      e.cd -= dt;
      if (e.cd <= 0 && dist(e.x, e.y, s.x, s.y) < 110) {
        const float b = atan2f(s.y-e.y, s.x-e.x);
        bullets_.push_back({ e.x, e.y, cosf(b)*ENEMY_BSPD, sinf(b)*ENEMY_BSPD, 2.5f, true });
        e.cd = 1.2f + frand()*1.2f;
      }
      if (dist(e.x, e.y, s.x, s.y) < 6) { s.hp -= 12; e.hp = 0; }   // ram
    }
    // Move bullets and resolve hits.
    for (size_t i = 0; i < bullets_.size();) {
      Bullet &b = bullets_[i];
      b.x += b.vx*dt; b.y += b.vy*dt; b.life -= dt;
      bool dead = b.life <= 0 || b.x < 0 || b.x > WW_ || b.y < 0 || b.y > WH_;
      if (!dead) {
        if (b.foe) { if (dist(b.x, b.y, s.x, s.y) < 5) { s.hp -= 8; dead = true; } }
        else for (Enemy &e : enemies_)
               if (e.hp > 0 && dist(b.x, b.y, e.x, e.y) < 5) { e.hp--; dead = true; break; }
      }
      if (dead) { bullets_[i] = bullets_.back(); bullets_.pop_back(); } else ++i;
    }
    for (size_t i = 0; i < enemies_.size();) {
      if (enemies_[i].hp <= 0) {
        const int g = enemies_[i].guard;     // a slain guardian unlocks its planet
        if (g >= 0 && g < (int)planets_.size() && planets_[g].guards > 0) planets_[g].guards--;
        enemies_[i] = enemies_.back(); enemies_.pop_back();
      } else ++i;
    }
    // The gate is invisible until the ship's sensors find it; then it can warp.
    if (!gateFound_ && dist(gx_, gy_, s.x, s.y) < 75) { gateFound_ = true; findFlash_ = 0.8f; }
    if (gateFound_ && dist(gx_, gy_, s.x, s.y) < 12) NextSystem();
    if (findFlash_ > 0) findFlash_ -= dt;
    if (s.hp <= 0) { s.hp = 0; deathT_ = 1.6f; }
    if (warpFlash_ > 0) warpFlash_ -= dt;
  }

  void FillCircle(Canvas *c, int cx, int cy, int r, uint8_t R, uint8_t G, uint8_t B) {
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (dx*dx + dy*dy <= r*r) c->SetPixel(cx+dx, cy+dy, R, G, B);
  }
  void DrawPlanet(Canvas *c, const Planet &p) {
    const int px = (int)(p.x - camx_), py = (int)(p.y - camy_), r = (int)p.r;
    const bool spent = (p.kind == 0 ? p.fuel : p.ore) <= 1;
    const float lx = -0.5f, ly = -0.55f, lz = 0.67f;   // light direction
    // Shaded, textured, banded sphere with a specular glint.
    for (int dy = -r; dy <= r; ++dy) {
      for (int dx = -r; dx <= r; ++dx) {
        if (dx*dx + dy*dy > r*r) continue;
        const float nx = dx/(float)r, ny = dy/(float)r;
        const float nz = sqrtf(std::max(0.0f, 1.0f - nx*nx - ny*ny));
        float ldot = nx*lx + ny*ly + nz*lz;
        if (ldot < 0) ldot = 0;
        const uint32_t hsh = Hash2((int)p.x + dx + 1000, (int)p.y + dy + 1000);
        const float tex = 0.85f + (hsh & 31)/31.0f*0.30f;
        const float band = 0.92f + 0.08f*sinf(dy*0.7f + p.x*0.1f);
        float m = (0.30f + 0.85f*ldot) * tex * band;
        if (spent) m *= 0.4f;
        const float l2 = ldot*ldot, l4 = l2*l2, spec = l4*l4*0.8f;
        c->SetPixel(px+dx, py+dy,
                    (uint8_t)clampi((int)(p.cr*m + 255*spec), 0, 255),
                    (uint8_t)clampi((int)(p.cg*m + 255*spec), 0, 255),
                    (uint8_t)clampi((int)(p.cb*m + 255*spec), 0, 255));
      }
    }
    if (p.ring) {                                      // Saturn-style ring
      const float a = r*1.9f, b2 = r*0.55f;
      const uint8_t rc = clampi(p.cr+50,0,255), gc = clampi(p.cg+50,0,255), bc = clampi(p.cb+50,0,255);
      for (int t = 0; t < 140; ++t) {
        const float th = t/140.0f*6.2831853f;
        const int ex = (int)(a*cosf(th)), ey = (int)(b2*sinf(th));
        if ((ex*ex + ey*ey <= r*r) && ey < 0) continue;   // behind the planet's top
        c->SetPixel(px+ex, py+ey, rc, gc, bc);
      }
    }
    if (p.guards > 0) {                                 // locked: pulsing red ring
      const uint8_t v = (uint8_t)(150 + 90*sinf(phase_*1.5f));
      rgb_matrix::DrawCircle(c, px, py, r + 3, rgb_matrix::Color(v, 30, 30));
    }
    // Progress bar above the planet: fuel/ammo remaining out of its capacity.
    const float frac = p.cap > 0 ? clampf((p.kind==0 ? p.fuel : p.ore)/p.cap, 0, 1) : 0;
    int bw = 2*r; if (bw < 10) bw = 10; if (bw > 22) bw = 22;
    const int bx = px - bw/2, by = py - r - (p.ring ? 6 : 4);
    for (int i = 0; i < bw; ++i) {
      const bool on = i < (int)(frac*bw);
      uint8_t br, bg, bb;
      if (!on) { br = 28; bg = 28; bb = 34; }
      else if (p.kind == 0) { br = 40; bg = 220; bb = 90; }   // fuel: green
      else { br = 235; bg = 170; bb = 40; }                    // ammo: amber
      c->SetPixel(bx+i, by, br, bg, bb);
      c->SetPixel(bx+i, by-1, br, bg, bb);
    }
  }
  void Tri(Canvas *c, float cx, float cy, float ang, float sz,
           uint8_t r, uint8_t g, uint8_t b) {
    const int nx = (int)(cx + cosf(ang)*sz),        ny = (int)(cy + sinf(ang)*sz);
    const int lx = (int)(cx + cosf(ang+2.5f)*sz),   ly = (int)(cy + sinf(ang+2.5f)*sz);
    const int rx = (int)(cx + cosf(ang-2.5f)*sz),   ry = (int)(cy + sinf(ang-2.5f)*sz);
    DrawLine(c, nx, ny, lx, ly, r, g, b);
    DrawLine(c, nx, ny, rx, ry, r, g, b);
    DrawLine(c, lx, ly, rx, ry, r, g, b);
    c->SetPixel((int)cx, (int)cy, r, g, b);
  }
  void Render(Canvas *c) {
    const int W = width_, H = height_;
    camx_ = clampf(ship_.x - W*0.5f, 0, (float)(WW_-W));
    camy_ = clampf(ship_.y - H*0.5f, 0, (float)(WH_-H));
    c->Fill(0, 0, 4);
    for (Star &st : stars_)
      c->SetPixel((int)(st.x-camx_), (int)(st.y-camy_), st.b/3, st.b/3, st.b/2);
    for (Planet &p : planets_) DrawPlanet(c, p);
    if (gateFound_) {                                             // gate: only once found
      const int gxp = (int)(gx_-camx_), gyp = (int)(gy_-camy_);
      const int extra = findFlash_ > 0 ? (int)(findFlash_*14) : 0;
      for (int rr = 4; rr <= 10 + extra; rr += 3) {
        const uint8_t v = (uint8_t)(120 + 100*sinf(phase_ + rr));
        rgb_matrix::DrawCircle(c, gxp, gyp, rr, rgb_matrix::Color(v/3, v/2, v));
      }
      c->SetPixel(gxp, gyp, 200, 240, 255);
    }
    for (Bullet &b : bullets_) {
      const int bx = (int)(b.x-camx_), by = (int)(b.y-camy_);
      if (b.foe) c->SetPixel(bx, by, 255, 80, 60);
      else c->SetPixel(bx, by, 120, 230, 255);
    }
    for (Enemy &e : enemies_)
      Tri(c, e.x-camx_, e.y-camy_, atan2f(ship_.y-e.y, ship_.x-e.x), 3.5f, 220, 50, 50);
    Tri(c, ship_.x-camx_, ship_.y-camy_, ship_.ang, 4.0f, 120, 220, 255);
    if (thrusting_)
      c->SetPixel((int)(ship_.x-camx_ - cosf(ship_.ang)*5),
                  (int)(ship_.y-camy_ - sinf(ship_.ang)*5), 255, 150, 40);
    if (deathT_ > 0) {
      const int ex = (int)(ship_.x-camx_), ey = (int)(ship_.y-camy_);
      const int rr = (int)((1.6f-deathT_)*30);
      rgb_matrix::DrawCircle(c, ex, ey, rr, rgb_matrix::Color(255, 120, 20));
      rgb_matrix::DrawCircle(c, ex, ey, rr/2, rgb_matrix::Color(255, 220, 80));
    }
    DrawHUD(c);
    phase_ += 0.15f;
  }
  void DrawHUD(Canvas *c) {
    const int W = width_;
    const int fw = (int)(W * ship_.fuel / ship_.maxfuel);
    const int hw = (int)(W * (float)ship_.hp / ship_.maxhp);
    for (int x = 0; x < W; ++x) {
      const bool f = x < fw, h = x < hw;
      c->SetPixel(x, 0, f?20:8, f?160:8, f?40:10);   // fuel bar (green)
      c->SetPixel(x, 1, f?20:8, f?160:8, f?40:10);
      c->SetPixel(x, 2, h?200:8, h?40:8, h?40:10);    // health bar (red)
      c->SetPixel(x, 3, h?200:8, h?40:8, h?40:10);
    }
    const int shown = std::min(ship_.ammo, 46);       // ammo pips (yellow, left)
    for (int i = 0; i < shown; ++i) c->SetPixel(2+i*2, height_-2, 230, 210, 40);
    for (int i = 0; i < system_ && i < 16; ++i) {     // system pips (cyan, right)
      const int x = width_-3-i*3;
      c->SetPixel(x, height_-2, 80, 200, 255);
      c->SetPixel(x+1, height_-2, 80, 200, 255);
    }
  }

  int WW_ = 560, WH_ = 400;
  Ship ship_;
  std::vector<Planet> planets_;
  std::vector<Enemy> enemies_;
  std::vector<Bullet> bullets_;
  std::vector<Star> stars_;
  float gx_ = 0, gy_ = 0, camx_ = 0, camy_ = 0, wx_ = 0, wy_ = 0;
  float fireCd_ = 0, enemyT_ = 0, warpFlash_ = 0, phase_ = 0, deathT_ = 0, findFlash_ = 0;
  int system_ = 0;
  bool thrusting_ = false, gateFound_ = false;
};

REGISTER_MODE(30, SpaceExplorerMode);
