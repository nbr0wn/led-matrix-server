// MineMode -- registered as mode 29.
#include "common/mode.h"

// --- Mine (side-view digging colony) --------------------------------------
// A cross-section of the earth: a pit-head with a spinning winding wheel, a
// central shaft with a lift cage, and miners who work one level at a time.
// Miners can only change depth by riding the lift -- they walk to the shaft,
// wait for the cage, board, ride, and step off at the level they want. They
// tunnel horizontally to ore veins (coal / iron / gold / gems), carry it back
// to the shaft, ride to the top and add it to a growing pile at the surface.
// Water seeps slowly and follows the tunnels downhill; miners cannot cross it,
// so a flooded level makes them look elsewhere. Lava seeps, rock caves in, and
// the sky runs a day/night cycle with stars.
class MineMode : public Mode {
public:
  const char *name() const override { return "Mine"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    cs_ = 2; gw_ = w / cs_; gh_ = h / cs_;
    surf_ = gh_ / 6; sx_ = gw_ / 2;
    // Lava is a static floor in the bottom two rows (it never spreads), so
    // miners can safely work every row above it, right down to botMine_.
    botMine_ = gh_ - 3;
    const int clo = kPileHalfW, chi = gw_ - 1 - kPileHalfW;  // keep each heap's zone on-panel
    pileBase_[PGOLD]   = clampi(sx_ - 38, clo, chi);   // three separate heaps along the surface,
    pileBase_[PSILVER] = clampi(sx_ - 14, clo, chi);   // one per ore type -- gold and silver set
    pileBase_[PGEM]    = clampi(sx_ + 18, clo, chi);   // wide apart, gems past the pit-head
    genSeed_ = 0;
    Regenerate();
  }
  // Build (or rebuild) a fresh mine: terrain, ore, miners, fog and empty heaps.
  void Regenerate() {
    g_.assign(gw_ * gh_, STONE);
    for (int y = 0; y < gh_; ++y)
      for (int x = 0; x < gw_; ++x) {
        int v;
        if (y < surf_) v = AIR;
        else if (y < surf_ + 3) v = DIRT;
        else if (y > gh_ - 3) v = LAVA;
        else v = OreAt(x, y);
        g_[y * gw_ + x] = v;
      }
    for (int y = surf_; y < gh_; ++y) g_[y * gw_ + sx_] = TUNNEL;   // the shaft
    miners_.clear();
    for (int i = 0; i < 7; ++i) {
      Miner m; m.x = sx_; m.y = surf_; m.tx = m.ty = m.goal = surf_;
      m.st = SEEK; m.carry = 0; m.face = 1; m.cool = i * 8; m.fail = 0; m.explore = 0;
      miners_.push_back(m);
    }
    score_ = 0; wheel_ = 0; sinceDeposit_ = 0;
    liftY_ = (float)surf_; liftDir_ = 1;
    for (int p = 0; p < 3; ++p) pileHt_[p].assign(gw_, 0);
    // Fog of war: the whole dig starts hidden and is revealed as miners' lamps
    // pass over it. The shaft itself is known infrastructure, so it starts lit.
    seen_.assign(gw_ * gh_, 0);
    light_.assign(gw_ * gh_, 0);
    explored_.assign(gh_, 0);
    for (int y = surf_; y < gh_; ++y) seen_[y * gw_ + sx_] = 1;
  }
  void Draw(Canvas *c, float t, float /*dt*/) override {
    const float day = 0.5f + 0.5f * sinf(t * 0.06f);
    if (restart_) { restart_ = false; ++genSeed_; Regenerate(); }   // mine was worked out: start over
    for (int i = 0; i < kTurbo; ++i) { Step(); Physics(); }   // TEMP(debug): kTurbo>1 runs fast
    Reveal();
    RenderTerrain(c, t);
    RenderSurface(c, t, day);
    RenderPile(c, t);
    RenderLift(c);
    for (Miner &m : miners_) RenderMiner(c, m);
  }
private:
  enum { AIR, TUNNEL, DIRT, STONE, GOLD, SILVER, GEM, LAVA, WATER, BEAM };
  enum { SEEK, TOSHAFT, WAITLIFT, RIDE, DIG, EXPLORE, DEPOSIT };
  // st drives the miner; goal is the level the lift should take them to. A
  // miner carries a single piece of ore at a time (carry): it mines one chunk,
  // rides it to the surface and drops it on the pile before going back down.
  // explore is set while the miner is prospecting an empty level rather than
  // heading for known ore.
  // A miner pauses kWalkDelay frames after stepping through open tunnel, but
  // kDigDelay frames after carving fresh ground -- so hewing takes real time.
  enum { kWalkDelay = 1, kDigDelay = 19, kLampR = 6 };
  enum { kTurbo = 10 };                  // TEMP(debug): sim steps per rendered frame; set back to 1
  enum { kPileHalfW = 9 };               // half-width of each ore heap's zone
  enum { kExhaust = 5000 };              // sim steps with no ore banked -> mine is worked out
  struct Miner { int x, y, tx, ty, st, carry, face, cool, goal, fail, explore; };
  int cs_, gw_, gh_, surf_, sx_, score_;
  int botMine_;                          // deepest row miners work: the last row above the lava
  int genSeed_ = 0;                      // varies the ore layout each time the mine restarts
  int sinceDeposit_ = 0;                 // sim steps since the last piece was banked
  bool restart_ = false;                 // set when the mine is worked out; regenerated next frame
  int pileBase_[3];                      // surface column of the gold / silver / gem heaps
  bool digStep_ = false;                 // did the last WalkH carve rock (vs. walk open)?
  float wheel_, liftY_;
  int liftDir_;
  std::vector<int> g_, flood_;           // flood_: water cells to fill this step
  std::vector<int> pileHt_[3];           // per-type pile heights per column: gold, silver, gem
  std::vector<uint8_t> seen_, light_, explored_;   // fog: ever-seen; per-frame lamp light; per-level worked
  std::vector<Miner> miners_;
  enum { PGOLD, PSILVER, PGEM };

  bool solid(int v) { return v == DIRT || v == STONE || v == GOLD || v == SILVER || v == GEM; }
  bool ore(int v) { return v == GOLD || v == SILVER || v == GEM; }
  bool blocked(int v) { return v == WATER || v == LAVA; }   // a miner can't pass these
  int at(int x, int y) { return (x < 0 || x >= gw_ || y < 0 || y >= gh_) ? STONE : g_[y * gw_ + x]; }
  void set(int x, int y, int v) { if (x >= 0 && x < gw_ && y >= 0 && y < gh_) g_[y * gw_ + x] = v; }
  int OreAt(int x, int y) {
    const uint32_t hsh = Hash2(x + genSeed_ * 101, y * 3 + genSeed_ * 17);   // reseed each restart
    const int depth = y - surf_;
    if ((hsh % 100) < (uint32_t)(6 + depth / 6)) {
      const int r = (hsh >> 8) % 100;
      if (depth > gh_ / 2 && r < 10) return GEM;    // colored gems: deep and rare
      if (r < 40) return GOLD;                       // gold veins
      return SILVER;                                 // silver: the common metal
    }
    if ((hsh % 1200) == 0 && depth > 8) return WATER;   // few, deep water pockets
    return STONE;
  }
  // Is ore cell idx already the target of another miner heading for it?
  bool OreTaken(int idx, const Miner &self) {
    const int tx = idx % gw_, ty = idx / gw_;
    for (const Miner &o : miners_) {
      if (&o == &self || o.explore || o.tx != tx || o.ty != ty) continue;
      if (o.st == DIG) return true;
      if ((o.st == TOSHAFT || o.st == WAITLIFT || o.st == RIDE) && o.goal > surf_) return true;
    }
    return false;
  }
  // Nearest *revealed* ore to (fx,fy) -- only ore a lamp has already lit counts
  // as visible loot. Ore another miner is already going for is skipped so miners
  // spread across a vein instead of all pouncing on one lump; if every visible
  // lump is taken it falls back to the nearest anyway. `any` picks a random vein
  // (used after a miss). -1 = none in sight, the cue to go prospecting.
  int FindOre(int fx, int fy, bool any, const Miner &self) {
    int best = -1, bd = 1 << 30, count = 0, pick = -1;         // untaken
    int aBest = -1, aBd = 1 << 30, aCount = 0, aPick = -1;     // any (incl. taken)
    for (int y = surf_; y <= botMine_; ++y)                    // never chase ore into the lava
      for (int x = 0; x < gw_; ++x) {                          // incl. the edge columns
        const int i = y * gw_ + x;
        if (!seen_[i] || !ore(g_[i])) continue;
        const bool taken = OreTaken(i, self);
        if (any) {
          if (rand() % (++aCount) == 0) aPick = i;
          if (!taken && rand() % (++count) == 0) pick = i;
        } else {
          const int d = abs(x - fx) * 2 + abs(y - fy);
          if (d < aBd) { aBd = d; aBest = i; }
          if (!taken && d < bd) { bd = d; best = i; }
        }
      }
    if (any) return pick >= 0 ? pick : aPick;
    return best >= 0 ? best : aBest;
  }
  // Pick a random level to prospect: one already worked (explored), or one still
  // dark, per `wantExplored`. Returns -1 if there is no such level.
  int PickDepth(bool wantExplored) {
    int count = 0, pick = -1;
    for (int y = surf_ + 1; y <= botMine_; ++y)
      if ((bool)explored_[y] == wantExplored && rand() % (++count) == 0) pick = y;
    return pick;
  }
  // Lamps: clear the per-frame light, then splat each miner's headlamp, marking
  // everything in radius as seen (permanent) and lit (this frame, for brightness).
  void Reveal() {
    std::fill(light_.begin(), light_.end(), 0);
    for (const Miner &m : miners_)
      for (int dy = -kLampR; dy <= kLampR; ++dy)
        for (int dx = -kLampR; dx <= kLampR; ++dx) {
          const int d2 = dx * dx + dy * dy;
          if (d2 > kLampR * kLampR) continue;
          const int x = m.x + dx, y = m.y + dy;
          if (x < 0 || x >= gw_ || y < 0 || y >= gh_) continue;
          const int i = y * gw_ + x;
          seen_[i] = 1;
          const int b = (int)(255.0f * (1.0f - sqrtf((float)d2) / (kLampR + 1)));
          if (b > light_[i]) light_[i] = (uint8_t)b;
        }
  }
  // Step one cell horizontally toward tx on the miner's level, digging solid
  // rock (leaving the odd support beam). Returns false if the way is blocked by
  // water or lava (or the edge) -- the caller then gives up on this target.
  bool WalkH(Miner &m, int tx) {
    digStep_ = false;
    if (m.x == tx) return true;
    const int nx = m.x + (tx > m.x ? 1 : -1);
    if (nx < 1 || nx > gw_ - 2) return false;
    const int v = at(nx, m.y);
    if (blocked(v)) return false;
    if (solid(v)) { set(nx, m.y, (Hash2(nx, m.y) % 9 == 0) ? BEAM : TUNNEL); digStep_ = true; }
    m.face = (nx > m.x) ? 1 : -1;
    m.x = nx;
    return true;
  }
  // Drop one piece of ore on its own type's surface heap (gold, silver or gem).
  // A block rolls off any slope steeper than 1, so each heap grows as a broad
  // slope-1 mound; it is confined to a fixed zone (base +/- kPileHalfW) so heaps
  // stay wide but separate. When the whole zone is packed to the sky it is full.
  void AddToPile(int oreType) {
    const int p = (oreType == GOLD) ? PGOLD : (oreType == SILVER) ? PSILVER : PGEM;
    std::vector<int> &H = pileHt_[p];
    const int lo = pileBase_[p] - kPileHalfW, hi = pileBase_[p] + kPileHalfW;
    int x = pileBase_[p];
    for (int guard = 0; guard < 2 * kPileHalfW + 2; ++guard) {
      const int hx = H[x];
      const int lh = (x > lo) ? H[x - 1] : 1 << 30;   // zone walls act as infinite height
      const int rh = (x < hi) ? H[x + 1] : 1 << 30;
      if (hx >= surf_) {                              // this column hit the sky: spill aside
        if (lh < hx && lh <= rh) { --x; continue; }
        if (rh < hx) { ++x; continue; }
        return;                                       // zone packed to the ceiling: no room
      }
      if (lh < hx && lh <= rh) { --x; continue; }     // keep the slope <= 1 (wide mound)
      if (rh < hx) { ++x; continue; }
      break;
    }
    if (x >= lo && x <= hi && H[x] < surf_) H[x]++;
  }
  // Send a miner to its chosen target: straight to work if already on the level,
  // otherwise to the shaft -- boarding on the spot if the cage is right here and
  // heading its way, so a miner can deposit and get back on in one step.
  void RouteTo(Miner &m) {
    if (m.y == m.ty) { m.st = m.explore ? EXPLORE : DIG; return; }
    m.goal = m.ty;
    const int dir = (m.goal > m.y) - (m.goal < m.y);
    if (m.x == sx_ && fabsf(liftY_ - m.y) < 0.9f && (dir == 0 || liftDir_ == dir))
      m.st = RIDE;
    else m.st = (m.x == sx_) ? WAITLIFT : TOSHAFT;
  }
  // Is another miner already working / heading to level y?
  bool LevelBusy(int y, const Miner &self) {
    for (const Miner &o : miners_)
      if (&o != &self && o.ty == y &&
          (o.st == DIG || o.st == EXPLORE || o.st == TOSHAFT || o.st == WAITLIFT || o.st == RIDE))
        return true;
    return false;
  }
  // Decide a miner's next job. It heads for visible loot first, then prospects
  // (75% an already-worked depth, 25% a fresh one) -- but preferring a level
  // that is neither where it just was nor where another miner is already
  // digging, so miners fan out instead of piling onto the same seam. A miner
  // that keeps failing to reach anything (fail hits the limit) stops fixating on
  // that unreachable ore and just picks a *random* level. fail is cleared only
  // by real progress (mining or carving ground), never here, so a bad target
  // can't trap it.
  void ChooseTask(Miner &m) {
    if (m.fail < 3) {                              // still willing to chase loot
      const int b = FindOre(m.x, m.y, m.fail >= 1, m);   // after a miss, try a random vein
      if (b >= 0) { m.explore = 0; m.tx = b % gw_; m.ty = b / gw_; RouteTo(m); return; }
    }
    int d = -1;
    for (int tries = 0; tries < 8; ++tries) {     // fan out: avoid our old level and busy ones
      int cand = -1;
      if (m.fail < 3) {
        const bool wantExplored = rand() % 4 != 0;
        cand = PickDepth(wantExplored);
        if (cand < 0) cand = PickDepth(!wantExplored);
      }
      if (cand < 0) cand = surf_ + 1 + rand() % (botMine_ - surf_);   // flailing / none left
      d = cand;
      if (cand != m.ty && !LevelBusy(cand, m)) break;
    }
    m.explore = 1; m.ty = d;
    m.tx = (rand() & 1) ? 1 : gw_ - 2;             // tunnel all the way to a random edge
    RouteTo(m);
  }
  void Step() {
    // When nobody has banked ore for a long stretch, every miner is coming back
    // empty-handed: the mine is worked out, so flag it to be regenerated.
    if (++sinceDeposit_ > kExhaust) restart_ = true;
    for (Miner &m : miners_) {
      if (m.cool > 0) { m.cool--; continue; }
      // Safety net: if a miner has failed to make progress far too many times
      // it has been walled in (by water, say) -- climb it back out to the shaft
      // top and start fresh, so nothing is ever permanently stuck.
      if (m.fail > 30) {
        m.x = sx_; m.y = surf_; m.carry = 0; m.explore = 0; m.fail = 0;
        m.goal = surf_; m.st = SEEK;
      }
      switch (m.st) {
      case SEEK:
        ChooseTask(m);
        break;
      case TOSHAFT:
        if (m.x == sx_) m.st = WAITLIFT;
        else if (!WalkH(m, sx_)) { m.st = SEEK; m.fail++; m.cool = 6; }
        else m.cool = digStep_ ? kDigDelay : kWalkDelay;
        break;
      case WAITLIFT: {                               // wait for the cage at this level
        m.x = sx_;
        const int dir = (m.goal > m.y) - (m.goal < m.y);   // board only when the cage
        if (fabsf(liftY_ - m.y) < 0.7f && (dir == 0 || liftDir_ == dir))
          { m.y = (int)lroundf(liftY_); m.st = RIDE; }     // is already heading our way
        break; }
      case RIDE:                                     // ride, and choose when to step off
        m.x = sx_; m.y = (int)lroundf(liftY_);
        if (m.y == m.goal) m.st = (m.goal <= surf_) ? DEPOSIT : (m.explore ? EXPLORE : DIG);
        break;
      case DIG:
        if (m.y != m.ty) { m.st = SEEK; break; }
        explored_[m.ty] = 1;
        if (!ore(at(m.tx, m.ty))) {                  // someone else grabbed our lump first:
          m.explore = 1;                             // don't turn back -- keep tunnelling ahead,
          m.tx = (m.tx >= m.x) ? gw_ - 2 : 1;        // on toward the edge in our travel direction
          m.st = EXPLORE; break;                     // (there is often more ore further along)
        }
        if (abs(m.x - m.tx) <= 1) {                  // reached the vein: take one piece
          m.carry = at(m.tx, m.ty); set(m.tx, m.ty, TUNNEL);
          m.fail = 0;                                // a real haul -- we're getting somewhere
          m.goal = surf_; m.st = TOSHAFT;            // haul this single piece up
          m.cool = kDigDelay;                        // hewing the ore takes a beat
        } else if (!WalkH(m, m.tx)) { m.st = SEEK; m.fail++; m.cool = 6; }
        else m.cool = digStep_ ? kDigDelay : kWalkDelay;
        break;
      case EXPLORE:                                  // prospect: tunnel outward, lamp revealing
        explored_[m.ty] = 1;
        if (m.fail < 3 && FindOre(m.x, m.y, false, m) >= 0) { m.explore = 0; m.st = SEEK; break; }  // struck loot
        if (m.x == m.tx || !WalkH(m, m.tx)) {        // dead end (edge, or wall of water/lava):
          if (m.x != m.tx) m.fail++;                 // stopped short: count it against us
          ChooseTask(m);                             // found nothing -- keep digging elsewhere
        } else { if (digStep_) m.fail = 0; m.cool = digStep_ ? kDigDelay : kWalkDelay; }
        break;
      case DEPOSIT:                                  // at the top: drop the piece and
        if (m.carry) { AddToPile(m.carry); m.carry = 0; score_++; m.fail = 0; sinceDeposit_ = 0; }
        ChooseTask(m);                               // re-board the waiting cage in one step
        break;
      }
    }
  }
  void Physics() {
    wheel_ += 0.2f;
    // The cage only descends as far as the deepest miner needs it -- the lowest
    // level anyone is working at or riding toward -- then turns back for the
    // surface, rather than running the whole shaft down to the lava.
    int bottom = surf_;
    for (const Miner &m : miners_) {
      if (m.y > bottom) bottom = m.y;
      if (m.goal > bottom) bottom = m.goal;
    }
    if (bottom > gh_ - 2) bottom = gh_ - 2;
    liftY_ += liftDir_ * 0.32f;                      // (2x)
    if (liftDir_ > 0 && liftY_ >= bottom) { liftY_ = (float)bottom; liftDir_ = -1; }
    if (liftDir_ < 0 && liftY_ <= surf_)  { liftY_ = (float)surf_;  liftDir_ = 1; }
    // (Lava no longer seeps up into tunnels -- it stays a static floor, so it
    // can never climb the mine and wall miners in.)
    // Water: slow, and it follows the tunnels downhill -- straight down where it
    // can, else sideways toward a tunnel that keeps descending, and only rarely
    // across a flat run. Each water cell gets an occasional (1-in-kWaterSlow)
    // nudge and may push into just one neighbour; targets are collected and
    // applied after the scan so it advances a cell at a time rather than
    // flash-flooding. The shaft is pumped, so water never enters the lift column.
    const int kWaterSlow = 18;
    flood_.clear();
    for (int y = surf_; y < gh_ - 1; ++y)
      for (int x = 1; x < gw_ - 1; ++x) {
        if (g_[y * gw_ + x] != WATER || rand() % kWaterSlow != 0) continue;
        int tgt = -1;
        if (at(x, y + 1) == TUNNEL && x != sx_) tgt = (y + 1) * gw_ + x;   // straight down
        else {
          const bool lTun = at(x - 1, y) == TUNNEL && x - 1 != sx_;
          const bool rTun = at(x + 1, y) == TUNNEL && x + 1 != sx_;
          const bool lDown = lTun && (at(x - 1, y + 1) == TUNNEL || at(x - 1, y + 1) == AIR);
          const bool rDown = rTun && (at(x + 1, y + 1) == TUNNEL || at(x + 1, y + 1) == AIR);
          if (lDown && rDown) tgt = y * gw_ + (rand() & 1 ? x - 1 : x + 1);
          else if (lDown) tgt = y * gw_ + (x - 1);
          else if (rDown) tgt = y * gw_ + (x + 1);
          else if (lTun && rand() % 3 == 0) tgt = y * gw_ + (x - 1);       // flat: creep, rarely
          else if (rTun && rand() % 3 == 0) tgt = y * gw_ + (x + 1);
        }
        if (tgt >= 0) flood_.push_back(tgt);
      }
    for (size_t i = 0; i < flood_.size(); ++i) g_[flood_[i]] = WATER;
    // Cave-ins (rare): unsupported stone drops into the tunnel below it.
    for (int k = 0; k < gw_ / 6; ++k) {
      const int x = 1 + rand() % (gw_ - 2), y = surf_ + rand() % (gh_ - surf_ - 2);
      if (g_[y * gw_ + x] == STONE && at(x, y + 1) == TUNNEL && x != sx_ && rand() % 500 == 0) {
        set(x, y, TUNNEL); set(x, y + 1, STONE);
      }
    }
  }
  void Cell(Canvas *c, int gx, int gy, int r, int g, int b) {
    for (int dy = 0; dy < cs_; ++dy) for (int dx = 0; dx < cs_; ++dx) Px(c, gx * cs_ + dx, gy * cs_ + dy, r, g, b);
  }
  void RenderTerrain(Canvas *c, float t) {
    for (int gy = surf_; gy < gh_; ++gy)
      for (int gx = 0; gx < gw_; ++gx) {
        const int i = gy * gw_ + gx;
        const uint32_t hsh = Hash2(gx, gy);
        if (!seen_[i]) {                              // fog of war: never lit -> hidden
          const int f = 7 + (int)(hsh & 5);
          Cell(c, gx, gy, f, f, f + 5);
          continue;
        }
        const int v = g_[i];
        const int n = (int)(hsh & 15) - 7;
        int r, g, b;
        switch (v) {
          case TUNNEL: r = 14; g = 10; b = 8; break;
          case DIRT:   r = 95 + n; g = 62 + n; b = 34 + n; break;
          case STONE:  r = 68 + n; g = 68 + n; b = 76 + n; break;
          case GOLD:   r = 225; g = 180; b = 45; break;
          case SILVER: r = 190 + ((hsh % 7 == 0) ? 45 : 0); g = 195 + ((hsh % 7 == 0) ? 40 : 0); b = 205; break;
          case GEM:  { uint8_t hr, hg, hb; HueToRGB(gx * 0.05f + gy * 0.03f + t * 0.1f, &hr, &hg, &hb); r = hr; g = hg; b = hb; break; }
          case WATER:  r = 40; g = 90 + (int)(20 * sinf(t * 2 + gx)); b = 180; break;
          case BEAM:   r = 110; g = 75; b = 40; break;
          case LAVA:   r = 255; g = 90 + (int)(40 * sinf(t * 4 + gx * 0.5f)); b = 10; break;
          default:     r = 60; g = 60; b = 66; break;
        }
        // Brightness = a dim "remembered" floor once seen, ramping to full under
        // a headlamp. Lava keeps a hot glow of its own even out past the lamps.
        float lf = 0.30f + 0.70f * (light_[i] / 255.0f);
        if (v == LAVA && lf < 0.75f) lf = 0.75f;
        r = (int)(r * lf); g = (int)(g * lf); b = (int)(b * lf);
        Cell(c, gx, gy, r, g, b);
      }
  }
  void RenderSurface(Canvas *c, float t, float day) {
    for (int gy = 0; gy < surf_; ++gy) {
      const float f = gy / (float)surf_;
      const int r = (int)((10 + 60 * day) * (0.3f + 0.7f * f));
      const int g = (int)((20 + 90 * day) * (0.3f + 0.7f * f));
      const int b = (int)((40 + 130 * day) * (0.4f + 0.6f * f));
      for (int gx = 0; gx < gw_; ++gx) {
        if (day < 0.4f && Hash2(gx, gy) % 53 == 0) Cell(c, gx, gy, 180, 180, 200);   // stars
        else Cell(c, gx, gy, r, g, b);
      }
    }
    const int sunx = (int)(gw_ * 0.8f), suny = (int)(surf_ * 0.4f);
    (day > 0.4f) ? Cell(c, sunx, suny, 255, 240, 180) : Cell(c, sunx, suny, 220, 220, 235);
    for (int gx = 0; gx < gw_; ++gx) Cell(c, gx, surf_, (int)(40 * day + 10), (int)(120 * day + 20), (int)(40 * day + 10));
    const int hx = sx_ * cs_, hy = surf_ * cs_;     // pit-head + winding wheel
    DrawLine(c, hx - 4, hy, hx - 4, hy - 14, 120, 90, 50);
    DrawLine(c, hx + 4, hy, hx + 4, hy - 14, 120, 90, 50);
    DrawLine(c, hx - 4, hy - 14, hx + 4, hy - 14, 120, 90, 50);
    DrawLine(c, hx - 4, hy, hx + 4, hy - 10, 120, 90, 50);
    for (int k = 0; k < 8; ++k) { const float a = wheel_ + k * 0.785f; Px(c, hx + (int)(cosf(a) * 4), hy - 14 + (int)(sinf(a) * 4), 210, 210, 70); }
  }
  void OreColor(int v, int &r, int &g, int &b) {
    switch (v) { case GOLD: r = 230; g = 185; b = 50; break; case SILVER: r = 205; g = 210; b = 220; break;
      case GEM: r = 140; g = 90; b = 255; break; default: r = g = b = 120; }
  }
  // The three ore heaps at the surface -- gold, silver, gems -- one block per
  // piece hauled up, stacking upward from the grass so each mound's size tracks
  // that type's total.
  void RenderPile(Canvas *c, float t) {
    static const int kType[3] = { GOLD, SILVER, GEM };
    for (int p = 0; p < 3; ++p) {
      int r0, g0, b0; OreColor(kType[p], r0, g0, b0);
      for (int x = 0; x < gw_; ++x)
        for (int h = 0; h < pileHt_[p][x]; ++h) {
          int r = r0, g = g0, b = b0;
          if (kType[p] == GEM) { uint8_t hr, hg, hb; HueToRGB(x * 0.1f + h * 0.05f + t * 0.1f, &hr, &hg, &hb); r = hr; g = hg; b = hb; }
          Cell(c, x, surf_ - 1 - h, r, g, b);
        }
    }
  }
  void RenderLift(Canvas *c) {
    const int lx = sx_ * cs_, ly = (int)(liftY_ * cs_);
    const int wy = surf_ * cs_ - 14;                // wheel hub at the pit-head
    DrawLine(c, lx, wy, lx, ly - 3, 90, 82, 72);    // hoist cable
    for (int dx = -2; dx <= 2; ++dx) {              // cage: top and bottom bars
      Px(c, lx + dx, ly - 3, 165, 165, 175);
      Px(c, lx + dx, ly + 2, 165, 165, 175);
    }
    for (int dy = -2; dy <= 1; ++dy) {              // cage sides
      Px(c, lx - 2, ly + dy, 150, 150, 160);
      Px(c, lx + 2, ly + dy, 150, 150, 160);
    }
  }
  void RenderMiner(Canvas *c, const Miner &m) {
    const int x = m.x * cs_, y = m.y * cs_;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)   // headlamp glow
      if (dx * dx + dy * dy <= 5) Px(c, x + dx + m.face * 2, y + dy, 90, 80, 40);
    Px(c, x, y, 255, 150, 40); Px(c, x + 1, y, 255, 150, 40);
    Px(c, x, y - 1, 255, 220, 120);
    if (m.carry) { int r, g, b; OreColor(m.carry, r, g, b); Px(c, x, y - 2, r, g, b); }
  }
};

REGISTER_MODE(28, MineMode);
