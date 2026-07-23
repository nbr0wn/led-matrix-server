// CityMode -- registered as mode 27.
#include "common/mode.h"

// --- City (organic, population-driven) ------------------------------------
// A city that grows from a single founding building. Population rises at a
// fixed rate; both the building count and the car count scale with it. New
// buildings bud off existing ones and branch outward with varied footprints,
// rising from foundation to full height -- taller toward the older downtown
// core. Cars roam the streets (the gaps between buildings), turning at
// junctions, with headlights at night; a day/night cycle lights the windows.
class CityMode : public Mode {
public:
  const char *name() const override { return "City"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    cs_ = 3; gw_ = w / cs_; gh_ = h / cs_;          // 3px cells -> 3px-wide roads
    occ_.assign(gw_ * gh_, -1);
    road_.assign(gw_ * gh_, 0);
    park_.assign(gw_ * gh_, 0);
    blds_.clear(); cars_.clear();
    pop_ = 0.0f;
    ccx_ = gw_ / 2.0f; ccy_ = gh_ / 2.0f;
    PlaceBuilding(gw_ / 2 - 1, gh_ / 2 - 1, 2, 2, true);   // the founding building
  }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    c->Fill(0, 0, 0);                               // cover the whole panel (3px grid leaves edges)
    const float day = 0.5f + 0.5f * sinf(t * 0.09f);
    const float lite = 0.30f + 0.70f * day;
    const bool night = day < 0.42f;

    pop_ += RATE_;                                  // population grows at a fixed rate
    const int wantB = 1 + (int)(pop_ / PBLD_);      // one new building per PBLD_ people
    const int wantC = 1 + (int)(pop_ / PCAR_);
    for (Bld &b : blds_) {                          // buildings rise, then fill with residents
      if (b.ht < b.maxht) b.ht += 0.02f + 0.015f * frand();
      // fill at the population rate divided by capacity: bigger buildings (more
      // footprint cells) need more people, so they take proportionally longer.
      if (b.fill < 1.0f) b.fill += RATE_ * 0.009f / (float)b.cap;
    }
    if ((int)blds_.size() < wantB && (int)blds_.size() < BCAP_) TryGrow();       // at most one/frame
    while ((int)cars_.size() < wantC && (int)cars_.size() < CCAP_) { Car v; if (SpawnCar(v)) cars_.push_back(v); else break; }
    if ((int)cars_.size() > wantC && !cars_.empty()) cars_.pop_back();

    for (int y = 0; y < gh_; ++y)                   // black bg; grey roads only beside buildings
      for (int x = 0; x < gw_; ++x) {
        const int i = y * gw_ + x;
        if (occ_[i] < 0 && road_[i]) {
          Cell(c, x, y, (int)(58 * lite), (int)(58 * lite), (int)(64 * lite));   // greyish road
          DrawCentreLine(c, x, y, lite);                                          // dashed yellow
        } else if (park_[i]) {
          const bool tree = (Hash2(x, y) % 7) == 0;
          Cell(c, x, y, (int)((tree ? 20 : 30) * lite), (int)((tree ? 70 : 105) * lite), (int)(30 * lite));   // parkland
        } else {
          Cell(c, x, y, 0, 0, 0);                    // black background
        }
      }
    for (const Bld &b : blds_) DrawBld(c, b, lite, night, t);
    if (night) DrawStreetlights(c);
    for (Car &v : cars_) { UpdateCar(v); DrawCar(c, v, night); }
  }
private:
  struct Bld { int fx, fy, fw, fh, zone, cap; float ht, maxht, fill; uint32_t seed; };
  struct Car { int cx, cy, idx, idy, odx, ody, ppx, ppy; float prog, spd; uint8_t r, g, b; bool emer; };
  float RATE_ = 0.5f, PBLD_ = 60.0f, PCAR_ = 30.0f;   // people, per building, per car (cars grow faster)
  int BCAP_ = 600, CCAP_ = 250;
  int cs_, gw_, gh_;
  float pop_, ccx_, ccy_;
  std::vector<int> occ_;
  std::vector<uint8_t> road_, park_;
  std::vector<Bld> blds_;
  std::vector<Car> cars_;

  // A drivable cell is an empty cell that has been marked road (roads exist only
  // in the 1-cell ring beside buildings, so cars never wander the wilderness).
  bool drivable(int x, int y) const { return x >= 0 && x < gw_ && y >= 0 && y < gh_ && occ_[y * gw_ + x] < 0 && road_[y * gw_ + x]; }
  void Cell(Canvas *c, int gx, int gy, int r, int g, int b) {
    for (int dy = 0; dy < cs_; ++dy) for (int dx = 0; dx < cs_; ++dx) Px(c, gx * cs_ + dx, gy * cs_ + dy, r, g, b);
  }
  // Dashed yellow centre line down a road cell, oriented to the corridor.
  // Static (keyed to cell parity, so it never scrolls). Skipped at junctions.
  void DrawCentreLine(Canvas *c, int gx, int gy, float lite) {
    const bool hz = drivable(gx - 1, gy) || drivable(gx + 1, gy);
    const bool vt = drivable(gx, gy - 1) || drivable(gx, gy + 1);
    const int yr = (int)(150 * lite), yg = (int)(135 * lite), yb = (int)(30 * lite);
    const int mid = cs_ / 2;
    // Short dashes keyed to world pixel coords (continuous across cells): a 2px
    // dash every 4px, so each street section shows several dashes.
    if (hz && !vt)
      for (int d = 0; d < cs_; ++d) { const int wx = gx * cs_ + d; if ((wx & 3) < 2) Px(c, wx, gy * cs_ + mid, yr, yg, yb); }
    else if (vt && !hz)
      for (int d = 0; d < cs_; ++d) { const int wy = gy * cs_ + d; if ((wy & 3) < 2) Px(c, gx * cs_ + mid, wy, yr, yg, yb); }
  }
  bool freeArea(int fx, int fy, int fw, int fh) const {    // footprint clear of buildings & roads; margin clear of buildings
    for (int y = fy - 1; y <= fy + fh; ++y)
      for (int x = fx - 1; x <= fx + fw; ++x) {
        if (x < 0 || x >= gw_ || y < 0 || y >= gh_) return false;
        if (occ_[y * gw_ + x] >= 0) return false;
        const bool foot = (x >= fx && x < fx + fw && y >= fy && y < fy + fh);
        if (foot && road_[y * gw_ + x]) return false;       // never build on top of a road
      }
    return true;
  }
  // Reclassify empty land: cells not reachable from the map border through
  // non-road empty cells are enclosed by the city -> parks. The open frontier
  // (reachable from the edge) stays black background.
  void ComputeParks() {
    // Keep roads a single cell wide: wherever four road cells form a 2x2 block
    // (a "plaza"), drop one so the road stays 3px. The dropped cell is enclosed
    // by the city, so the flood-fill below turns it into parkland.
    for (int y = 0; y + 1 < gh_; ++y)
      for (int x = 0; x + 1 < gw_; ++x) {
        const int i = y * gw_ + x;
        if (road_[i] && road_[i + 1] && road_[i + gw_] && road_[i + gw_ + 1]) road_[i + gw_ + 1] = 0;
      }
    std::vector<uint8_t> open(gw_ * gh_, 0);
    std::vector<int> q;
    auto land = [&](int x, int y) { return x >= 0 && x < gw_ && y >= 0 && y < gh_ && occ_[y * gw_ + x] < 0 && !road_[y * gw_ + x]; };
    for (int x = 0; x < gw_; ++x) { if (land(x, 0)) { open[x] = 1; q.push_back(x); }
                                    if (land(x, gh_ - 1)) { open[(gh_ - 1) * gw_ + x] = 1; q.push_back((gh_ - 1) * gw_ + x); } }
    for (int y = 0; y < gh_; ++y) { if (land(0, y)) { open[y * gw_] = 1; q.push_back(y * gw_); }
                                    if (land(gw_ - 1, y)) { open[y * gw_ + gw_ - 1] = 1; q.push_back(y * gw_ + gw_ - 1); } }
    static const int D[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (size_t qi = 0; qi < q.size(); ++qi) {
      const int cx = q[qi] % gw_, cy = q[qi] / gw_;
      for (int k = 0; k < 4; ++k) {
        const int ax = cx + D[k][0], ay = cy + D[k][1];
        if (!land(ax, ay) || open[ay * gw_ + ax]) continue;
        open[ay * gw_ + ax] = 1; q.push_back(ay * gw_ + ax);
      }
    }
    for (int i = 0; i < gw_ * gh_; ++i) park_[i] = (occ_[i] < 0 && !road_[i] && !open[i]) ? 1 : 0;
  }
  void PlaceBuilding(int fx, int fy, int fw, int fh, bool founder) {
    Bld b; b.fx = fx; b.fy = fy; b.fw = fw; b.fh = fh; b.cap = fw * fh;
    const int r = rand() % 100;
    b.zone = r < 8 ? 3 : (r < 58 ? 0 : (r < 83 ? 1 : 2));   // park / res / com / ind
    const float dx = fx + fw * 0.5f - ccx_, dy = fy + fh * 0.5f - ccy_;
    const float dist = sqrtf(dx * dx + dy * dy);
    const float central = fmaxf(0.0f, fminf(1.0f, 1.0f - dist / (gw_ * 0.42f)));
    b.maxht = (b.zone == 3) ? 1.0f : (2.0f + central * 16.0f + rand() % 4);   // downtown = taller
    b.ht = founder ? b.maxht * 0.5f : 0.0f;
    b.fill = founder ? 0.4f : 0.0f;
    b.seed = Hash2(fx * 7 + 1, fy * 13 + 5);
    const int idx = (int)blds_.size();
    for (int y = fy; y < fy + fh; ++y) for (int x = fx; x < fx + fw; ++x) occ_[y * gw_ + x] = idx;
    // roads appear only beside buildings: mark the 1-cell perimeter as drivable
    for (int y = fy - 1; y <= fy + fh; ++y)
      for (int x = fx - 1; x <= fx + fw; ++x)
        if (x >= 0 && x < gw_ && y >= 0 && y < gh_ && occ_[y * gw_ + x] < 0) road_[y * gw_ + x] = 1;
    blds_.push_back(b);
    ComputeParks();                                 // enclosed leftover land becomes parkland
  }
  // Bud a new building off a random existing one, branching outward with a
  // 1-cell street gap and a varied footprint (bigger footprints as the city grows).
  void TryGrow() {
    static const int S[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    const int maxf = (pop_ > 1000.0f) ? 4 : 3;
    for (int tries = 0; tries < 10; ++tries) {
      const Bld &b = blds_[rand() % blds_.size()];
      const int fw = 2 + rand() % (maxf - 1), fh = 2 + rand() % (maxf - 1);   // min 2x2
      const int s = rand() % 4;
      int nx, ny;
      if (S[s][0] == 1)       { nx = b.fx + b.fw + 1; ny = b.fy + rand() % b.fh - fh / 2; }
      else if (S[s][0] == -1) { nx = b.fx - fw - 1;   ny = b.fy + rand() % b.fh - fh / 2; }
      else if (S[s][1] == 1)  { ny = b.fy + b.fh + 1; nx = b.fx + rand() % b.fw - fw / 2; }
      else                    { ny = b.fy - fh - 1;   nx = b.fx + rand() % b.fw - fw / 2; }
      if (freeArea(nx, ny, fw, fh)) { PlaceBuilding(nx, ny, fw, fh, false); return; }
    }
  }
  bool SpawnCar(Car &v) {
    static const int D[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int tries = 0; tries < 40; ++tries) {
      const int x = rand() % gw_, y = rand() % gh_;
      if (!drivable(x, y)) continue;
      for (int q = 0; q < 4; ++q) {
        const int k = rand() % 4;
        if (!drivable(x + D[k][0], y + D[k][1])) continue;
        v.cx = x; v.cy = y; v.idx = D[k][0]; v.idy = D[k][1];
        v.prog = frand() * 1.5f; v.spd = 0.25f + frand() * 0.2f;   // pixels per frame
        v.ppx = -1000;                              // no previous position yet
        v.emer = (rand() % 40 == 0);
        if (v.emer) { v.r = v.g = v.b = 255; v.spd *= 1.6f; }
        else { uint8_t hr, hg, hb; HueToRGB(frand(), &hr, &hg, &hb); v.r = hr / 2 + 100; v.g = hg / 2 + 100; v.b = hb / 2 + 100; }
        ChooseOut(v);
        return true;
      }
    }
    return false;
  }
  // Choose the direction to leave the current cell, given how we entered it
  // (v.idx,v.idy). No U-turn, exit must be drivable, prefer going straight.
  void ChooseOut(Car &v) {
    static const int D[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    int cand[4], nc = 0, straight = -1;
    for (int k = 0; k < 4; ++k) {
      if (D[k][0] == -v.idx && D[k][1] == -v.idy) continue;
      if (!drivable(v.cx + D[k][0], v.cy + D[k][1])) continue;
      cand[nc++] = k;
      if (D[k][0] == v.idx && D[k][1] == v.idy) straight = k;
    }
    if (nc == 0) { v.odx = -v.idx; v.ody = -v.idy; return; }   // dead end: turn back
    const int k = (straight >= 0 && rand() % 100 < 72) ? straight : cand[rand() % nc];
    v.odx = D[k][0]; v.ody = D[k][1];
  }
  // Length of the lane path through the current cell (a cell width when straight,
  // a quarter-arc when turning) so cars keep a constant linear speed everywhere.
  // The three key pixels of the car's path through its current 3px cell: the
  // entry pixel (on the -idir edge, right lane), the exit pixel (on the +odir
  // edge, right lane), and the pivot = where the entry and exit lanes cross.
  // Inside turns collapse to entry==pivot==exit (a single corner pixel); outside
  // turns have the pivot at the OUTER corner, reached by walking two full sides.
  void Nodes(const Car &v, int &epx, int &epy, int &xpx, int &xpy, int &pvx, int &pvy) const {
    const int x0 = v.cx * cs_ + 1, y0 = v.cy * cs_ + 1;   // centre pixel of the cell
    epx = x0 - v.idx - v.idy; epy = y0 + v.idx - v.idy;
    xpx = x0 + v.odx - v.ody; xpy = y0 + v.odx + v.ody;
    if (v.idx != 0) { pvx = x0 - v.ody; pvy = y0 + v.idx; }   // idir horizontal, odir vertical
    else            { pvx = x0 - v.idy; pvy = y0 + v.odx; }   // idir vertical, odir horizontal
  }
  int PathLen(const Car &v) const {                          // pixel count of the path
    int epx, epy, xpx, xpy, pvx, pvy; Nodes(v, epx, epy, xpx, xpy, pvx, pvy);
    if (v.idx * v.odx + v.idy * v.ody != 0)                  // straight or U-turn: entry -> exit
      return 1 + abs(xpx - epx) + abs(xpy - epy);
    return 1 + abs(pvx - epx) + abs(pvy - epy) + abs(xpx - pvx) + abs(xpy - pvy);   // via pivot
  }
  int BuildPath(const Car &v, int *xs, int *ys) const {      // fill the pixel list, return count
    int epx, epy, xpx, xpy, pvx, pvy; Nodes(v, epx, epy, xpx, xpy, pvx, pvy);
    int n = 1, cx = epx, cy = epy; xs[0] = cx; ys[0] = cy;
    if (v.idx * v.odx + v.idy * v.ody != 0) {                // straight / U-turn: walk to exit
      const int sx = (xpx > cx) - (xpx < cx), sy = (xpy > cy) - (xpy < cy);
      while ((cx != xpx || cy != xpy) && n < 7) { cx += sx; cy += sy; xs[n] = cx; ys[n] = cy; ++n; }
    } else {                                                 // turn: walk entry->pivot->exit
      while ((cx != pvx || cy != pvy) && n < 7) { cx += v.idx; cy += v.idy; xs[n] = cx; ys[n] = cy; ++n; }
      while ((cx != xpx || cy != xpy) && n < 7) { cx += v.odx; cy += v.ody; xs[n] = cx; ys[n] = cy; ++n; }
    }
    return n;
  }
  void UpdateCar(Car &v) {
    v.prog += v.spd;                                    // advance one step of pixel distance
    const int n = PathLen(v);
    if (v.prog < n) return;                             // still walking this cell's pixels
    v.prog -= n;
    const int ncx = v.cx + v.odx, ncy = v.cy + v.ody;   // leave via the exit direction
    if (drivable(ncx, ncy)) { v.cx = ncx; v.cy = ncy; v.idx = v.odx; v.idy = v.ody; }
    ChooseOut(v);                                       // decide how to leave the new cell
  }
  // The car sits on exact grid pixels: it walks the entry->(pivot)->exit pixel
  // list one pixel at a time. Inside corners touch a single pixel; outside turns
  // trace two whole sides of the 3x3 intersection through the outer corner.
  void DrawCar(Canvas *c, Car &v, bool night) {
    int xs[8], ys[8];
    const int n = BuildPath(v, xs, ys);
    int i = (int)v.prog; if (i >= n) i = n - 1; if (i < 0) i = 0;
    const int px = xs[i], py = ys[i];
    uint8_t r = v.r, g = v.g, b = v.b;
    if (v.emer) { const bool bl = ((int)(px + py) & 4); r = bl ? 255 : 30; g = 30; b = bl ? 30 : 255; }
    // Connect to last frame's pixel so the trail is unbroken across cell borders.
    if (v.ppx > -1000) DrawLine(c, v.ppx, v.ppy, px, py, r, g, b);
    else Px(c, px, py, r, g, b);
    v.ppx = px; v.ppy = py;
    if (night || v.emer) Px(c, px + v.odx, py + v.ody, 255, 250, 210);   // headlight
  }
  void DrawBld(Canvas *c, const Bld &b, float lite, bool night, float t) {
    const int x0 = b.fx * cs_, y0 = b.fy * cs_;
    const int x1 = (b.fx + b.fw) * cs_ - 1, y1 = (b.fy + b.fh) * cs_ - 1;
    if (b.zone == 3) {                               // park
      for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const bool tree = (Hash2(x, y) % 9) == 0;
        Px(c, x, y, (int)((tree ? 25 : 38) * lite), (int)((tree ? 75 : 120) * lite), (int)(35 * lite));
      }
      return;
    }
    if (b.ht < 0.7f) {                               // foundation / under construction
      for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
        Px(c, x, y, (int)(46 * lite), (int)(38 * lite), (int)(28 * lite));
      const int cxp = (x0 + x1) / 2;                 // crane
      DrawLine(c, cxp, y0, cxp, y1, 120, 100, 30);
      DrawLine(c, cxp, y0, x1, y0, 150, 130, 40);
      if ((int)(t * 4) & 1) Px(c, x1, y0, 255, 120, 0);
      return;
    }
    int zr, zg, zb;
    if (b.zone == 0) { zr = 150; zg = 120; zb = 95; }        // residential
    else if (b.zone == 1) { zr = 90; zg = 120; zb = 170; }   // commercial glass
    else { zr = 120; zg = 120; zb = 125; }                   // industrial
    const float hf = b.ht / fmaxf(1.0f, b.maxht);
    const float bright = (0.42f + 0.58f * hf) * lite;         // taller = brighter (no 3D offset)
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
      Px(c, x, y, (int)(zr * bright), (int)(zg * bright), (int)(zb * bright));
    // Occupancy: lit "resident" dots that fill in as this building's population
    // grows -- a continuous, visible indicator between new-building events.
    const int fillN = (int)(b.fill * 100);
    const float dl = night ? 1.0f : 0.7f;            // brighter at night
    for (int y = y0; y <= y1; y += 2)
      for (int x = x0; x <= x1; x += 2) {
        const uint32_t hsh = Hash2(x * 5 + (int)b.seed, y * 7);
        if ((int)(hsh % 100) < fillN)
          (b.zone == 1) ? Px(c, x, y, (int)(150 * dl), (int)(205 * dl), (int)(255 * dl))
                        : Px(c, x, y, (int)(255 * dl), (int)(205 * dl), (int)(125 * dl));
      }
  }
  void DrawStreetlights(Canvas *c) {
    // static warm lamps on street cells that border a building (never animated)
    for (int y = 0; y < gh_; ++y)
      for (int x = 0; x < gw_; ++x) {
        if (occ_[y * gw_ + x] >= 0 || !road_[y * gw_ + x] || (Hash2(x, y) % 11) != 0) continue;
        const bool border = (x + 1 < gw_ && occ_[y * gw_ + x + 1] >= 0) || (x > 0 && occ_[y * gw_ + x - 1] >= 0) ||
                            (y + 1 < gh_ && occ_[(y + 1) * gw_ + x] >= 0) || (y > 0 && occ_[(y - 1) * gw_ + x] >= 0);
        if (border) Px(c, x * cs_ + cs_ / 2, y * cs_ + cs_ / 2, 255, 220, 140);
      }
  }
};

REGISTER_MODE(26, CityMode);
