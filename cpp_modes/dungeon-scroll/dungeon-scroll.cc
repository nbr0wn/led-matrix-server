// DungeonScrollMode -- registered as mode 32.
#include "common/mode.h"

// --- 33: Dungeon crawler, big tiles + scrolling camera --------------------
// Autonomous roguelike rendered with 8x8 tiles on a larger map (64x40 tiles)
// than fits on screen. The viewport is a 24x16-tile camera that follows the
// hero, scrolling around the dungeon as it explores.
class DungeonScrollMode : public Mode {
public:
  const char *name() const override { return "Dungeon"; }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    TS_ = 8; gw_ = 64; gh_ = 40;   // 8px tiles; map larger than the screen
    camx_ = 0; camy_ = 0;
    if (!fontTried_) {
      fontTried_ = true;
      const char *cands[] = { "fonts/5x7.bdf", "fonts/6x10.bdf",
                              "/root/led-matrix-server/fonts/5x7.bdf" };
      for (const char *p : cands) if (font_.LoadFont(p)) { fontOK_ = true; break; }
    }
    NewGame();
  }
  void Draw(Canvas *c, float t, float dt) override {
    if (deathT_ > 0) {
      Render(c, true);
      if (--deathT_ == 0) NewGame();
      return;
    }
    if (++turnT_ >= 5) { turnT_ = 0; DoTurn(); }
    Render(c, false);
  }
private:
  struct Ent { int x, y, hp, mhp, atk, def, kind; };
  struct Item { int x, y, kind; };

  void NewGame() {
    depth_ = 0;
    player_.mhp = 26; player_.hp = 26; player_.atk = 6;
    player_.def = 0; player_.kind = 0;
    turnT_ = 0; deathT_ = 0;
    Descend();
  }
  void Descend() {
    ++depth_;        // deeper floors have tougher enemies (see GenFloor); the
    GenFloor();      // hero keeps all stats and HP between levels (no reset/heal)
  }
  void Carve(int x, int y) {
    if (x > 0 && x < gw_ - 1 && y > 0 && y < gh_ - 1) map_[y * gw_ + x] = 1;
  }
  void GenFloor() {
    map_.assign(gw_ * gh_, 0);
    std::vector<int> rcx, rcy;
    for (int attempts = 0; (int)rcx.size() < 9 && attempts < 90; ++attempts) {
      const int rw = 4 + rand() % 6, rh = 3 + rand() % 5;
      const int rx = 1 + rand() % std::max(1, gw_ - rw - 2);
      const int ry = 1 + rand() % std::max(1, gh_ - rh - 2);
      for (int y = ry; y < ry + rh; ++y)
        for (int x = rx; x < rx + rw; ++x) Carve(x, y);
      rcx.push_back(rx + rw / 2); rcy.push_back(ry + rh / 2);
    }
    for (size_t i = 1; i < rcx.size(); ++i) {
      const int x1 = rcx[i-1], y1 = rcy[i-1], x2 = rcx[i], y2 = rcy[i];
      for (int x = std::min(x1,x2); x <= std::max(x1,x2); ++x) Carve(x, y1);
      for (int y = std::min(y1,y2); y <= std::max(y1,y2); ++y) Carve(x2, y);
    }
    player_.x = rcx.front(); player_.y = rcy.front();
    anchorx_ = player_.x; anchory_ = player_.y; stuck_ = 0;   // reset stuck detection
    sx_ = rcx.back(); sy_ = rcy.back();
    mon_.clear();
    const int mcount = std::min(22, 4 + depth_);       // grows modestly each level
    for (int i = 0; i < mcount; ++i) {
      Ent m; int tries = 0;
      do { m.x = 1 + rand() % (gw_-2); m.y = 1 + rand() % (gh_-2); ++tries; }
      while ((map_[m.y*gw_+m.x] == 0 ||
              abs(m.x-player_.x) + abs(m.y-player_.y) < 6) && tries < 40);
      if (map_[m.y*gw_+m.x] == 0) continue;
      m.mhp = 3 + depth_ * 2; m.hp = m.mhp; m.atk = 2 + depth_ / 2;   // gentler than before
      m.def = 0; m.kind = rand() % 14;
      mon_.push_back(m);
    }
    items_.clear();
    const int icount = 2 + rand() % 4;
    for (int i = 0; i < icount; ++i) {
      Item it; int tries = 0;
      do { it.x = 1 + rand() % (gw_-2); it.y = 1 + rand() % (gh_-2); ++tries; }
      while (map_[it.y*gw_+it.x] == 0 && tries < 40);
      if (map_[it.y*gw_+it.x] == 0) continue;
      const int r = rand() % 100;
      it.kind = r < 50 ? 0 : (r < 70 ? 1 : (r < 85 ? 2 : 3));
      items_.push_back(it);
    }
  }

  bool Occupied(int x, int y) {
    if (player_.x == x && player_.y == y) return true;
    for (Ent &m : mon_) if (m.x == x && m.y == y) return true;
    return false;
  }
  int MonAdjacentTo(int x, int y) {
    for (size_t i = 0; i < mon_.size(); ++i)
      if (abs(mon_[i].x-x) + abs(mon_[i].y-y) == 1) return (int)i;
    return -1;
  }
  void DoTurn() {
    static const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
    // Stuck detection: if the hero leaves a 10-tile radius, re-anchor here;
    // otherwise count turns. After 30 turns without ranging out, force it to
    // head for the exit (see target selection below).
    if (std::max(abs(player_.x - anchorx_), abs(player_.y - anchory_)) > 10) {
      anchorx_ = player_.x; anchory_ = player_.y; stuck_ = 0;
    } else if (stuck_ < 1000000) {
      ++stuck_;
    }
    for (Ent &m : mon_) {
      if (abs(m.x-player_.x) + abs(m.y-player_.y) == 1) {
        int dmg = 1 + rand() % m.atk - player_.def;
        if (dmg < 1) dmg = 1;
        player_.hp -= dmg;
        if (player_.hp <= 0) { deathT_ = 100; return; }
      } else {
        int best = -1, bestd = 1<<30;
        const bool wander = (rand()%100) < 30;
        for (int k = 0; k < 4; ++k) {
          const int ax = m.x+dx[k], ay = m.y+dy[k];
          if (ax<0||ax>=gw_||ay<0||ay>=gh_) continue;
          if (map_[ay*gw_+ax] == 0 || Occupied(ax,ay)) continue;
          const int d = wander ? rand() : abs(ax-player_.x)+abs(ay-player_.y);
          if (d < bestd) { bestd = d; best = k; }
        }
        if (best >= 0) { m.x += dx[best]; m.y += dy[best]; }
      }
    }
    const int adj = MonAdjacentTo(player_.x, player_.y);
    if (adj >= 0) {
      mon_[adj].hp -= 1 + rand() % player_.atk;
      if (mon_[adj].hp <= 0) mon_.erase(mon_.begin() + adj);
      return;
    }
    if (player_.x == sx_ && player_.y == sy_) { Descend(); return; }
    int tx = sx_, ty = sy_;                       // default target: the exit
    const bool lowHp = player_.hp * 2 < player_.mhp;   // below 50% health
    const bool forceExit = stuck_ >= 30;          // been loitering: make for the stairs
    if (lowHp) {
      // Hurt: head for the nearest healing potion (kind 0); if there are none,
      // leave tx,ty at the exit.
      int bd = 1<<30, bi = -1;
      for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].kind != 0) continue;
        const int d = abs(items_[i].x-player_.x) + abs(items_[i].y-player_.y);
        if (d < bd) { bd = d; bi = (int)i; }
      }
      if (bi >= 0) { tx = items_[bi].x; ty = items_[bi].y; }
    } else if (!forceExit && !items_.empty()) {
      int bd = 1<<30, bi = 0;
      for (size_t i = 0; i < items_.size(); ++i) {
        const int d = abs(items_[i].x-player_.x) + abs(items_[i].y-player_.y);
        if (d < bd) { bd = d; bi = (int)i; }
      }
      tx = items_[bi].x; ty = items_[bi].y;
    }
    int nx, ny;
    bool moved = NextStep(player_.x, player_.y, tx, ty, nx, ny) ||
                 NextStep(player_.x, player_.y, sx_, sy_, nx, ny);
    if (!moved && !mon_.empty()) {   // path blocked (e.g. monster in corridor):
      int bd = 1<<30, bi = 0;         // fight toward the nearest monster
      for (size_t i = 0; i < mon_.size(); ++i) {
        const int d = abs(mon_[i].x-player_.x) + abs(mon_[i].y-player_.y);
        if (d < bd) { bd = d; bi = (int)i; }
      }
      moved = NextStep(player_.x, player_.y, mon_[bi].x, mon_[bi].y, nx, ny);
    }
    if (!moved) {                     // last resort: any free step, never freeze
      static const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
      int cand[4], nc = 0;
      for (int k = 0; k < 4; ++k) {
        const int ax = player_.x+dx[k], ay = player_.y+dy[k];
        if (ax<0||ax>=gw_||ay<0||ay>=gh_) continue;
        if (map_[ay*gw_+ax] && !Occupied(ax,ay)) cand[nc++] = k;
      }
      if (nc) { const int k = cand[rand()%nc]; nx = player_.x+dx[k]; ny = player_.y+dy[k]; moved = true; }
    }
    if (!moved) return;
    player_.x = nx; player_.y = ny;
    if (++regenT_ >= 10) {                         // slow regen: +1 HP per 10 steps
      regenT_ = 0;
      if (player_.hp < player_.mhp) ++player_.hp;
    }
    for (size_t i = 0; i < items_.size(); ++i)
      if (items_[i].x == player_.x && items_[i].y == player_.y) {
        switch (items_[i].kind) {
        case 0: player_.hp = std::min(player_.mhp, player_.hp + player_.mhp / 2); break;
        case 1: player_.atk += 2; break;
        case 2: player_.mhp += 6; player_.hp += 6; break;
        case 3: if (player_.def < 6) ++player_.def; break;
        }
        items_.erase(items_.begin() + i); break;
      }
    if (player_.x == sx_ && player_.y == sy_) Descend();
  }
  bool NextStep(int sx, int sy, int tx, int ty, int &nx, int &ny) {
    if (sx == tx && sy == ty) return false;
    const int N = gw_*gh_, start = sy*gw_+sx;
    std::vector<int> came(N, -2);
    std::vector<int> q; q.reserve(N);
    came[start] = -1; q.push_back(start);
    static const int dx[4] = {1,-1,0,0}, dy[4] = {0,0,1,-1};
    size_t qi = 0; bool found = false;
    while (qi < q.size()) {
      const int cur = q[qi++], cx = cur%gw_, cy = cur/gw_;
      if (cx == tx && cy == ty) { found = true; break; }
      for (int k = 0; k < 4; ++k) {
        const int ax = cx+dx[k], ay = cy+dy[k];
        if (ax<0||ax>=gw_||ay<0||ay>=gh_) continue;
        const int ai = ay*gw_+ax;
        if (came[ai] != -2 || map_[ai] == 0) continue;
        if (!(ax==tx && ay==ty) && Occupied(ax,ay)) continue;
        came[ai] = cur; q.push_back(ai);
      }
    }
    if (!found) return false;
    int cur = ty*gw_+tx;
    while (came[cur] != start) { cur = came[cur]; if (cur < 0) return false; }
    nx = cur%gw_; ny = cur/gw_; return true;
  }

  // ---- rendering (camera-relative) ----
  void Tile(Canvas *c, int tx, int ty, uint8_t r, uint8_t g, uint8_t b) {
    const int ox = (tx-camx_)*TS_, oy = (ty-camy_)*TS_;
    for (int py = 0; py < TS_; ++py)
      for (int px = 0; px < TS_; ++px)
        c->SetPixel(ox+px, oy+py, r, g, b);
  }
  // 4x4 sprite scaled to fill the tile (each cell -> TS_/4 pixels).
  void Sprite(Canvas *c, int tx, int ty, const char rows[4][5],
              uint8_t r, uint8_t g, uint8_t b) {
    const int ox = (tx-camx_)*TS_, oy = (ty-camy_)*TS_;
    const int sc = TS_/4 < 1 ? 1 : TS_/4;
    for (int cy = 0; cy < 4; ++cy)
      for (int cx = 0; cx < 4; ++cx)
        if (rows[cy][cx] == '1')
          for (int py = 0; py < sc; ++py)
            for (int px = 0; px < sc; ++px)
              c->SetPixel(ox+cx*sc+px, oy+cy*sc+py, r, g, b);
  }
  // Dim, per-floor wall base color (distinct hue each level, never bright).
  void WallBase(int *r, int *g, int *b) {
    uint8_t hr, hg, hb;
    HueToRGB(depth_ * 0.16f, &hr, &hg, &hb);
    *r = 16 + (int)hr * 34 / 255;
    *g = 16 + (int)hg * 34 / 255;
    *b = 20 + (int)hb * 34 / 255;
  }
  // Textured wall tile. `style` (cycling per floor) picks the pattern; the
  // texture is keyed to world pixel coords so it scrolls with the walls.
  void WallTile(Canvas *c, int tx, int ty, int br, int bg, int bb, int style) {
    const int ox = (tx-camx_)*TS_, oy = (ty-camy_)*TS_;
    for (int py = 0; py < TS_; ++py)
      for (int px = 0; px < TS_; ++px) {
        const int gx = tx*TS_+px, gy = ty*TS_+py;
        const uint32_t h = Hash2(gx, gy);
        const int s = h & 255;
        int add = ((h >> 8) & 7) - 3;          // subtle base noise
        switch (style) {
        case 0:                                 // fine speckle
          if (s < 26) add = 55; else if (s > 236) add = -14;
          break;
        case 1:                                 // regular dot grid
          if ((gx & 3) == 1 && (gy & 3) == 1) add = 60;
          else if (s < 10) add = 30;
          break;
        case 2:                                 // staggered brick / mortar
          if ((gy & 3) == 0) add = -14;
          else if (((gx + (gy >> 2) * 3) & 7) == 0) add = -14;
          else if (s < 14) add = 40;
          break;
        default:                                // cave blotches
          if (s < 10) add = 65; else if (s < 44) add = 22;
          break;
        }
        c->SetPixel(ox+px, oy+py, (uint8_t)clampi(br+add, 0, 255),
                    (uint8_t)clampi(bg+add, 0, 255), (uint8_t)clampi(bb+add, 0, 255));
      }
  }
  // Draw an 8x8 sprite (rows of '1'/'0') at a tile, camera-relative.
  void Sprite8(Canvas *c, int tx, int ty, const char rows[8][9],
               uint8_t r, uint8_t g, uint8_t b) {
    const int ox = (tx-camx_)*TS_, oy = (ty-camy_)*TS_;
    for (int py = 0; py < 8 && py < TS_; ++py)
      for (int px = 0; px < 8 && px < TS_; ++px)
        if (rows[py][px] == '1') c->SetPixel(ox+px, oy+py, r, g, b);
  }
  // Draw an 8x8 sprite at absolute pixel coords (for the HUD icons).
  void Icon8(Canvas *c, int px, int py, const char rows[8][9],
             uint8_t r, uint8_t g, uint8_t b) {
    for (int yy = 0; yy < 8; ++yy)
      for (int xx = 0; xx < 8; ++xx)
        if (rows[yy][xx] == '1' && px+xx >= 0 && px+xx < width_ && py+yy >= 0 && py+yy < height_)
          c->SetPixel(px+xx, py+yy, r, g, b);
  }
  void Render(Canvas *c, bool dead) {
    const int vw = width_ / TS_, vh = height_ / TS_;
    camx_ = clampi(player_.x - vw/2, 0, std::max(0, gw_ - vw));  // follow hero
    camy_ = clampi(player_.y - vh/2, 0, std::max(0, gh_ - vh));
    int br, bg, bb; WallBase(&br, &bg, &bb);
    const int style = depth_ & 3;
    for (int vy = 0; vy < vh; ++vy)
      for (int vx = 0; vx < vw; ++vx) {
        const int tx = camx_+vx, ty = camy_+vy;
        if (tx < 0 || tx >= gw_ || ty < 0 || ty >= gh_) continue;
        if (map_[ty*gw_+tx]) Tile(c, tx, ty, 0, 0, 0);       // floor: always black
        else WallTile(c, tx, ty, br, bg, bb, style);          // wall: per-floor texture
      }
    // --- down-stairs (descending steps) ---
    static const char STAIRS[8][9] = {
      "00000011", "00000111", "00001111", "00011111",
      "00111111", "01111111", "11111111", "00000000" };
    const uint8_t sp = (uint8_t)(150 + 100 * ((turnT_>>1)&1));
    Sprite8(c, sx_, sy_, STAIRS, 60, 130, sp);
    // --- items: flask (health), sword (str), heart (max HP), shield (armor) ---
    static const char ISPR[4][8][9] = {
      { "00011000","00111100","00011000","00111100","01111110","01111110","01111110","00111100" },
      { "00011000","00011000","00011000","00011000","00011000","01111110","00011000","00011000" },
      { "01100110","11111111","11111111","11111111","01111110","00111100","00011000","00000000" },
      { "01111110","11111111","11111111","11111111","01111110","01111110","00111100","00011000" },
    };
    static const uint8_t ICOL[4][3] = {
      { 40, 230, 90 }, { 255, 170, 40 }, { 255, 80, 160 }, { 90, 150, 255 },
    };
    for (Item &it : items_) {
      const int k = it.kind & 3;
      Sprite8(c, it.x, it.y, ISPR[k], ICOL[k][0], ICOL[k][1], ICOL[k][2]);
    }
    // --- monsters: 14 distinct 8x8 shapes ---
    static const char MSPR[14][8][9] = {
      { "10100101","01011010","00111100","01111110","01111110","00111100","01011010","10100101" }, // spider
      { "11000011","11100111","11111111","01111110","00100100","00000000","00000000","00000000" }, // bat
      { "00111100","01000010","01011010","01010010","01001110","01000000","01111100","00000000" }, // snake
      { "00111100","01111110","11011011","11111111","11111111","11111111","10101010","01010101" }, // ghost
      { "00000000","00011000","00111100","01111110","01111110","11111111","11111111","01111110" }, // slime
      { "00111100","01011010","00111100","00011000","01111110","00011000","00100100","01000010" }, // skeleton
      { "00011110","00111111","11101100","01111110","00111100","01100110","11000011","00000000" }, // dragon
      { "00111100","01111110","11100111","11011011","11100111","01111110","00111100","00000000" }, // eyeball
      { "00000000","00000010","01110110","11111110","11111100","01011000","00000100","00000000" }, // rat
      { "00010000","00110000","01111100","11111110","01111100","00110000","00010000","00000000" }, // bird
      { "01111110","11111111","11011011","11111111","11111111","11111111","11011011","01011010" }, // golem
      { "00100100","01011010","00111100","01111110","00111100","01000010","10000001","00011000" }, // scorpion
      { "00111100","01111110","01100110","01111110","00111100","00111100","00111100","00100100" }, // wraith
      { "00111100","01111110","11111111","11111111","00011000","00011000","00111100","00000000" }, // mushroom
    };
    static const uint8_t MCOL[14][3] = {
      {200,40,40}, {150,60,200}, {120,200,40}, {210,230,255}, {60,200,90},
      {230,230,210}, {230,120,30}, {220,60,200}, {150,110,70}, {80,200,220},
      {140,130,110}, {220,200,50}, {120,80,180}, {220,60,80},
    };
    for (Ent &m : mon_) {
      const int k = m.kind % 14;
      const float f = 0.55f + 0.45f * m.hp / (float)m.mhp;
      Sprite8(c, m.x, m.y, MSPR[k],
              (uint8_t)(MCOL[k][0]*f), (uint8_t)(MCOL[k][1]*f), (uint8_t)(MCOL[k][2]*f));
    }
    // --- the hero (a little man) ---
    static const char PLAYER[8][9] = {
      "00011000","00011000","01111110","00011000","00011000","00011000","00100100","01100110" };
    const float hf = player_.hp / (float)player_.mhp;
    Sprite8(c, player_.x, player_.y, PLAYER,
            (uint8_t)(255*(1-hf)*0.8f+40), (uint8_t)(230*hf), 50);
    // Stats read-out in the upper-right corner: heart (HP), sword (attack),
    // shield (armor) icons each followed by their value. HP number shifts
    // green -> red as health drops. A dark backing keeps it legible.
    if (fontOK_) {
      char hpn[8], atn[8], arn[8];
      snprintf(hpn, sizeof hpn, "%d", player_.hp);
      snprintf(atn, sizeof atn, "%d", player_.atk);
      snprintf(arn, sizeof arn, "%d", player_.def);
      int cw = font_.CharacterWidth('0'); if (cw <= 0) cw = 5;
      const int iconW = 8, gap = 1, grpgap = 4;
      const int wHp = iconW + gap + (int)strlen(hpn) * cw;
      const int wAt = iconW + gap + (int)strlen(atn) * cw;
      const int wAr = iconW + gap + (int)strlen(arn) * cw;
      const int total = wHp + grpgap + wAt + grpgap + wAr;
      int x = width_ - total - 2;
      const int by = font_.baseline();
      const int bh = (font_.height() > 8 ? font_.height() : 8) + 1;
      for (int yy = 0; yy < bh && yy < height_; ++yy)     // dark backing box
        for (int xx = x - 1; xx < width_; ++xx)
          if (xx >= 0) c->SetPixel(xx, yy, 0, 0, 0);
      const rgb_matrix::Color hpc((uint8_t)(255*(1-hf)*0.85f+45), (uint8_t)(230*hf+25), 70);
      Icon8(c, x, 0, ISPR[2], 235, 70, 80);              // heart
      rgb_matrix::DrawText(c, font_, x + iconW + gap, by, hpc, hpn);
      x += wHp + grpgap;
      Icon8(c, x, 0, ISPR[1], 255, 175, 50);             // sword
      rgb_matrix::DrawText(c, font_, x + iconW + gap, by, rgb_matrix::Color(255, 195, 100), atn);
      x += wAt + grpgap;
      Icon8(c, x, 0, ISPR[3], 90, 150, 255);             // shield
      rgb_matrix::DrawText(c, font_, x + iconW + gap, by, rgb_matrix::Color(150, 185, 255), arn);
    }
    for (int i = 0; i < depth_ && i < 20; ++i) {
      c->SetPixel(2+i*3, height_-1, 80, 160, 255);
      c->SetPixel(3+i*3, height_-1, 80, 160, 255);
    }
    if (dead) {
      const uint8_t r = ((deathT_>>3)&1) ? 220 : 80;
      for (int x = 0; x < width_; ++x) { c->SetPixel(x,0,r,0,0); c->SetPixel(x,height_-1,r,0,0); }
      for (int y = 0; y < height_; ++y) { c->SetPixel(0,y,r,0,0); c->SetPixel(width_-1,y,r,0,0); }
    }
  }
  int TS_, gw_, gh_, camx_, camy_;
  std::vector<uint8_t> map_;
  Ent player_;
  std::vector<Ent> mon_;
  std::vector<Item> items_;
  int sx_, sy_, depth_, turnT_, deathT_;
  int anchorx_ = 0, anchory_ = 0, stuck_ = 0;   // stuck detection -> head for exit
  int regenT_ = 0;                               // steps since last regen tick
  rgb_matrix::Font font_;
  bool fontTried_ = false, fontOK_ = false;
};

REGISTER_MODE(31, DungeonScrollMode);
