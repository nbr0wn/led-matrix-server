// BlackboardMode -- registered as mode 28.
#include "common/mode.h"

// --- Blackboard (panning chalk cartoons) -----------------------------------
// A slate that pans slowly to the left while an unseen hand keeps it filled:
// line-art doodles are laid down stroke by stroke in wobbly, grainy chalk (two
// lanes, plus the occasional full-height piece), captioned in a stroke-cut
// chalk alphabet. Chalk dust falls from the tip to the tray along the bottom.
//
// Everything is vector data: each doodle is a list of polylines built in a
// local ~34-unit-tall design box, resampled and jittered once so the lines look
// hand-drawn, tilted a few degrees off square, then revealed a stroke-length at
// a time. A piece is only started once it fits entirely in view, so no stroke
// is ever wasted off the edge. The board texture is a cached value-noise smudge
// field that scrolls with the camera, so the chalk grain and the eraser haze
// stay glued to the board as it moves.
class BlackboardMode : public Mode {
public:
  const char *name() const override { return "Blackboard"; }

  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    trayY_ = std::max(8, h - 5);
    nlane_ = (h >= 96) ? 2 : 1;
    if (nlane_ == 2) { lane_[0] = trayY_ * 0.28f; lane_[1] = trayY_ * 0.73f; }
    else             { lane_[0] = trayY_ * 0.50f; lane_[1] = lane_[0]; }
    laneH_ = (nlane_ == 2) ? trayY_ * 0.40f : trayY_ * 0.70f;
    cam_ = 0.0f; nextId_ = 1; activeId_ = -1;
    front_[0] = front_[1] = 8.0f;
    doodles_.clear(); pend_.clear(); dust_.clear(); bag_.clear();
    headX_ = headY_ = -100.0f;
    smudge_.assign((size_t)width_ * trayY_, 0);
    smudgeCam_ = 0;
    for (int x = 0; x < width_; ++x) FillSmudgeCol(x, 0);
    Prefill();
  }

  void Draw(Canvas *c, float t, float dt) override {
    cam_ += kScroll_ * dt;
    RollSmudge();
    DrawBoard(c);
    Spawn();
    Advance(dt);
    headX_ = headY_ = -100.0f;
    for (const Doodle &d : doodles_) DrawDoodle(c, d);
    UpdateDust(dt);
    DrawDust(c);
    if (headX_ > -50.0f) DrawChalkStick(c, t);
    DrawTray(c);
  }

private:
  struct Pt { float x, y; };
  struct Stroke { std::vector<Pt> p; };
  struct Doodle {
    std::vector<Stroke> s;
    float wx, cy, scale, w, total, drawn, rate;
    int id, lane;
    bool tall;
    uint8_t r, g, b;
  };
  struct Dust { float x, y, vx, vy, life, max; };

  const float kScroll_ = 5.0f;      // px/sec the board pans left
  const float kGap_    = 6.0f;      // px of blank board between doodles
  const float kDotLen_ = 1.2f;      // stroke-length a single-point dot costs

  int trayY_ = 0, nlane_ = 2, nextId_ = 1, activeId_ = -1;
  float lane_[2] = {0, 0}, front_[2] = {0, 0};
  float laneH_ = 0, cam_ = 0, headX_ = -100, headY_ = -100;
  std::vector<Doodle> doodles_, pend_;
  std::vector<Dust> dust_;
  std::vector<int> bag_;
  std::vector<uint8_t> smudge_;
  int smudgeCam_ = 0;

  // ---- geometry helpers (build into a raw stroke list) --------------------
  static void Poly(std::vector<Stroke> &s, std::initializer_list<float> v) {
    Stroke st;
    const float *p = v.begin();
    for (size_t i = 0; i + 1 < v.size(); i += 2) st.p.push_back({p[i], p[i + 1]});
    if (st.p.size()) s.push_back(st);
  }
  static void Line(std::vector<Stroke> &s, float x0, float y0, float x1, float y1) {
    Poly(s, {x0, y0, x1, y1});
  }
  static void Dot(std::vector<Stroke> &s, float x, float y) { Poly(s, {x, y}); }
  static void Ell(std::vector<Stroke> &s, float cx, float cy, float rx, float ry,
                  float a0 = 0.0f, float a1 = 6.28319f) {
    Stroke st;
    const int n = std::max(7, (int)((rx + ry) * fabsf(a1 - a0) * 0.20f));
    for (int i = 0; i <= n; ++i) {
      const float a = a0 + (a1 - a0) * i / n;
      st.p.push_back({cx + cosf(a) * rx, cy + sinf(a) * ry});
    }
    s.push_back(st);
  }

  // ---- a 3x5 stroke alphabet ----------------------------------------------
  // Each glyph is '|'-separated polylines of "xy" grid points (x 0..2, y 0..4,
  // y downward). Drawn with the same machinery as the pictures, so captions
  // appear letter by letter just like the line art.
  static const char *Glyph(char ch) {
    switch (ch) {
    case 'A': return "04 01 10 21 24|02 22";
    case 'B': return "04 00 10 21 12 02|12 23 14 04";
    case 'C': return "20 10 01 03 14 24";
    case 'D': return "00 04|00 10 21 23 14 04";
    case 'E': return "20 00 04 24|02 12";
    case 'F': return "20 00 04|02 12";
    case 'G': return "20 10 01 03 14 24 22 12";
    case 'H': return "00 04|20 24|02 22";
    case 'I': return "00 20|10 14|04 24";
    case 'J': return "20 23 14 04 03";
    case 'K': return "00 04|20 02|02 24";
    case 'L': return "00 04 24";
    case 'M': return "04 00 12 20 24";
    case 'N': return "04 00 24 20";
    case 'O': return "10 01 03 14 23 21 10";
    case 'P': return "04 00 10 21 12 02";
    case 'Q': return "10 01 03 14 23 21 10|13 24";
    case 'R': return "04 00 10 21 12 02|12 24";
    case 'S': return "20 10 01 12 23 14 04";
    case 'T': return "00 20|10 14";
    case 'U': return "00 03 14 23 20";
    case 'V': return "00 14 20";
    case 'W': return "00 04 12 24 20";
    case 'X': return "00 24|20 04";
    case 'Y': return "00 12 20|12 14";
    case 'Z': return "00 20 04 24";
    case '0': return "10 01 03 14 23 21 10|20 04";
    case '1': return "01 10 14|04 24";
    case '2': return "01 10 21 12 04 24";
    case '3': return "01 10 21 12|12 23 14 03";
    case '4': return "20 02 22|20 24";
    case '5': return "20 00 02 12 23 14 04";
    case '6': return "20 10 01 03 14 23 12 02";
    case '7': return "00 20 14";
    case '8': return "10 01 12 03 14 23 12 21 10";
    case '9': return "12 01 10 21 22 13 04";
    case '+': return "11 13|02 22";
    case '-': return "02 22";
    case '=': return "01 21|03 23";
    case '!': return "10 12|14";
    case '?': return "01 10 21 12 13|14";
    case '.': return "14";
    case ',': return "13 04";
    case ':': return "11|13";
    case '*': return "01 23|21 03|02 22";
    case '/': return "24 00";
    case '\'': return "10 11";
    default:  return nullptr;   // space and anything unknown
    }
  }
  static float TextW(const char *txt, float u) {
    const float n = (float)strlen(txt);
    return n > 0 ? n * 3.4f * u - 1.4f * u : 0.0f;
  }
  static void Text(std::vector<Stroke> &s, const char *txt, float x, float y, float sz) {
    const float u = sz / 4.0f;              // grid unit (glyphs are 4 units tall)
    float cx = x;
    for (const char *p = txt; *p; ++p) {
      const char *g = Glyph(*p);
      if (g) {
        Stroke st;
        for (const char *q = g;; ++q) {
          if (*q == '|' || *q == 0) {
            if (st.p.size()) s.push_back(st);
            st.p.clear();
            if (*q == 0) break;
          } else if (*q != ' ') {
            st.p.push_back({cx + (q[0] - '0') * u, y + (q[1] - '0') * u});
            ++q;
          }
        }
      }
      cx += 3.4f * u;
    }
  }

  // ---- the doodle catalogue ------------------------------------------------
  enum { HOUSE, CAT, TREE, ROCKET, FISH, PERSON, FLOWER, STAR, BOAT, CAR, SUN,
         RAINCLOUD, MUSHROOM, SNAIL, BALLOON, ROBOT, GHOST, BUTTERFLY, OWL,
         HEART, BULB, CUPCAKE, TRAIN, UMBRELLA, MOON, DOG, WORDS, NKIND };

  static const char *BuildArt(int kind, std::vector<Stroke> &v) {
    switch (kind) {
    case HOUSE:
      Poly(v, {2, 12, 2, 30, 26, 30, 26, 12});
      Poly(v, {-1, 13, 14, 2, 29, 13});
      Poly(v, {10, 30, 10, 21, 16, 21, 16, 30});
      Poly(v, {5, 16, 5, 22, 9, 22, 9, 16, 5, 16});
      Line(v, 5, 19, 9, 19); Line(v, 7, 16, 7, 22);
      Poly(v, {19, 16, 19, 22, 23, 22, 23, 16, 19, 16});
      Line(v, 19, 19, 23, 19); Line(v, 21, 16, 21, 22);
      Poly(v, {20, 8, 20, 4, 23, 4, 23, 10});
      Poly(v, {21.5f, 2, 19.5f, 0, 23, -2, 20, -4.5f});
      return "HOME";
    case CAT:
      Ell(v, 12, 9, 7, 6.5f);
      Poly(v, {6.5f, 4.5f, 5, -1, 11, 3});
      Poly(v, {17.5f, 4.5f, 19, -1, 13, 3});
      Dot(v, 9.5f, 8); Dot(v, 14.5f, 8);
      Poly(v, {12, 10.5f, 10.8f, 12, 13.2f, 12, 12, 10.5f});
      Poly(v, {9, 13, 12, 14.5f, 15, 13});
      Line(v, 1, 10, 6, 11.5f); Line(v, 1, 14, 6, 12.8f);
      Line(v, 23, 10, 18, 11.5f); Line(v, 23, 14, 18, 12.8f);
      Poly(v, {6, 15, 3, 26, 5, 31, 19, 31, 21, 26, 18, 15});
      Poly(v, {21, 30, 27, 28, 29, 22, 26, 18});
      return "MEOW";
    case TREE:
      Poly(v, {10, 32, 12, 19}); Poly(v, {18, 32, 16, 19});
      Line(v, 10, 32, 18, 32);
      Ell(v, 14, 12, 11, 9);
      Ell(v, 6, 15, 5, 4.2f, 1.4f, 4.9f);
      Ell(v, 22, 15, 5, 4.2f, -1.7f, 1.7f);
      Line(v, 13, 19, 9, 14); Line(v, 15, 19, 19, 15);
      return "TREE";
    case ROCKET:
      Poly(v, {8, 26, 8, 10, 12, 1, 16, 10, 16, 26, 8, 26});
      Ell(v, 12, 12, 2.6f, 2.6f);
      Poly(v, {8, 18, 2, 28, 8, 26});
      Poly(v, {16, 18, 22, 28, 16, 26});
      Poly(v, {9, 26, 10, 33, 12, 28, 14, 34, 15, 26});
      Poly(v, {24, 4, 26, 6}); Poly(v, {26, 4, 24, 6});
      Poly(v, {0, 9, 2, 11}); Poly(v, {2, 9, 0, 11});
      return "ZOOM";
    case FISH:
      Poly(v, {4, 14, 9, 7, 16, 5, 22, 9, 25, 14, 22, 19, 16, 23, 9, 21, 4, 14});
      Poly(v, {4, 14, -3, 7, -2, 14, -3, 21, 4, 14});
      Ell(v, 19, 12, 1.7f, 1.7f);
      Poly(v, {12, 6, 14, 1, 19, 7});
      Poly(v, {13, 22, 15, 26, 19, 20});
      Poly(v, {9, 8, 7, 14, 9, 20});
      Ell(v, 28, 8, 1.8f, 1.8f); Ell(v, 31, 3, 1.2f, 1.2f);
      return "BLUB";
    case PERSON:
      Ell(v, 12, 6, 5, 5);
      Dot(v, 10, 5); Dot(v, 14, 5);
      Poly(v, {9, 8, 12, 10, 15, 8});
      Line(v, 12, 11, 12, 22);
      Poly(v, {4, 20, 12, 15});
      Poly(v, {12, 15, 19, 8});
      Poly(v, {17, 4, 19.5f, 6}); Poly(v, {21, 4, 22, 7});
      Poly(v, {5, 31, 12, 22, 19, 31});
      return "HI!";
    case FLOWER:
      Poly(v, {14, 33, 14, 30, 13, 24, 15, 17});
      Poly(v, {14, 27, 7, 23, 6, 27, 13, 29});
      Poly(v, {14, 24, 21, 21, 22, 26, 15, 27});
      for (int i = 0; i < 6; ++i) {
        const float a = i * 1.0472f;
        Ell(v, 14 + cosf(a) * 6.4f, 11 + sinf(a) * 6.4f, 4.1f, 4.1f);
      }
      Ell(v, 14, 11, 2.6f, 2.6f);
      return "";
    case STAR: {
      Stroke st;
      for (int i = 0; i <= 10; ++i) {
        const float a = -1.5708f + i * 0.62832f;
        const float r = (i & 1) ? 5.6f : 13.0f;
        st.p.push_back({14 + cosf(a) * r, 15 + sinf(a) * r});
      }
      v.push_back(st);
      Dot(v, 14, 15);
      return "STAR"; }
    case BOAT:
      Poly(v, {1, 23, 5, 30, 27, 30, 31, 23, 1, 23});
      Line(v, 16, 23, 16, 1);
      Poly(v, {17, 4, 28, 21, 17, 21});
      Poly(v, {15, 6, 5, 21, 15, 21});
      Poly(v, {16, 1, 22, 3, 16, 5});
      Poly(v, {-3, 34, 1, 32, 5, 34, 9, 32, 13, 34, 17, 32, 21, 34, 25, 32, 29, 34, 33, 32});
      return "AHOY";
    case CAR:
      Poly(v, {1, 24, 3, 17, 11, 17, 15, 10, 26, 10, 31, 17, 37, 18, 38, 24, 1, 24});
      Poly(v, {16, 16, 17, 12, 24, 12, 27, 16, 16, 16});
      Line(v, 21, 12, 21, 16);
      Ell(v, 9, 24, 4.2f, 4.2f); Ell(v, 30, 24, 4.2f, 4.2f);
      Dot(v, 9, 24); Dot(v, 30, 24);
      Line(v, -2, 29, 41, 29);
      return "BEEP";
    case SUN:
      Ell(v, 15, 15, 8.5f, 8.5f);
      for (int i = 0; i < 12; ++i) {
        const float a = i * 0.5236f;
        Line(v, 15 + cosf(a) * 10.5f, 15 + sinf(a) * 10.5f,
                15 + cosf(a) * 15.5f, 15 + sinf(a) * 15.5f);
      }
      Dot(v, 12, 13); Dot(v, 18, 13);
      Ell(v, 15, 16, 4, 3.4f, 0.35f, 2.79f);
      return "";
    case RAINCLOUD:
      Ell(v, 10, 12, 6.5f, 5.5f, 3.1416f, 6.2832f);
      Ell(v, 20, 10, 7.5f, 7.0f, 3.1416f, 6.2832f);
      Ell(v, 28, 12, 5.5f, 4.5f, 3.1416f, 6.2832f);
      Poly(v, {3.5f, 12, 33.5f, 12});
      Poly(v, {8, 16, 5, 23}); Poly(v, {15, 15, 12, 22});
      Poly(v, {22, 16, 19, 23}); Poly(v, {29, 15, 26, 22});
      Poly(v, {12, 26, 9, 33}); Poly(v, {25, 26, 22, 33});
      return "";
    case MUSHROOM:
      Ell(v, 14, 13, 13, 10, 3.1416f, 6.2832f);
      Line(v, 1, 13, 27, 13);
      Poly(v, {10, 13, 9, 26, 12, 30, 17, 30, 19, 26, 18, 13});
      Ell(v, 8, 8, 2.4f, 1.9f); Ell(v, 17, 6, 3, 2.3f); Ell(v, 21, 11, 2, 1.6f);
      return "";
    case SNAIL: {
      Stroke st;
      for (int i = 0; i <= 46; ++i) {
        const float a = i * 0.36f, r = 0.8f + a * 0.58f;
        st.p.push_back({17 - cosf(a) * r, 14 - sinf(a) * r});
      }
      v.push_back(st);
      Poly(v, {28, 28, 6, 28, 2, 25, 3, 20, 7, 18});
      Poly(v, {4, 20, 2, 13}); Ell(v, 2, 12, 1.2f, 1.2f);
      Poly(v, {8, 18, 9, 12}); Ell(v, 9, 11, 1.2f, 1.2f);
      Line(v, -2, 29, 32, 29);
      return "SLOW"; }
    case BALLOON:
      Ell(v, 13, 11, 9, 10.5f);
      Poly(v, {13, 21.5f, 10.5f, 24, 15.5f, 24, 13, 21.5f});
      Poly(v, {13, 24, 17, 27, 10, 30, 16, 33, 12, 36});
      Poly(v, {9, 6, 11, 4, 14, 4});
      return "";
    case ROBOT:
      Poly(v, {8, 8, 8, 2, 20, 2, 20, 8, 8, 8});
      Line(v, 14, 2, 14, -3); Ell(v, 14, -4, 1.6f, 1.6f);
      Ell(v, 11, 5, 1.6f, 1.6f); Ell(v, 17, 5, 1.6f, 1.6f);
      Line(v, 11, 7, 17, 7);
      Poly(v, {6, 10, 6, 24, 22, 24, 22, 10, 6, 10});
      Poly(v, {9, 13, 9, 17, 13, 17, 13, 13, 9, 13});
      Dot(v, 17, 14); Dot(v, 17, 17); Dot(v, 20, 14);
      Poly(v, {6, 12, 1, 16, 1, 21});
      Poly(v, {22, 12, 27, 16, 27, 21});
      Poly(v, {10, 24, 10, 31, 6, 31});
      Poly(v, {18, 24, 18, 31, 22, 31});
      return "BZZT";
    case GHOST:
      Ell(v, 13, 12, 10, 10.5f, 3.1416f, 6.2832f);
      Poly(v, {3, 12, 3, 26, 6, 22, 9.5f, 26, 13, 22, 16.5f, 26, 20, 22, 23, 26, 23, 12});
      Ell(v, 9, 10, 2.1f, 2.7f); Ell(v, 17, 10, 2.1f, 2.7f);
      Ell(v, 13, 16, 2.4f, 3.0f);
      return "BOO!";
    case BUTTERFLY:
      Poly(v, {14, 6, 14, 25});
      Poly(v, {14, 6, 9, 0}); Poly(v, {14, 6, 19, 0});
      Ell(v, 7, 11, 7, 6); Ell(v, 21, 11, 7, 6);
      Ell(v, 8, 21, 5.5f, 5); Ell(v, 20, 21, 5.5f, 5);
      Dot(v, 6, 10); Dot(v, 22, 10);
      return "";
    case OWL:
      Poly(v, {5, 11, 4, 26, 9, 31, 19, 31, 24, 26, 23, 11});
      Ell(v, 14, 11, 9.5f, 9, 3.1416f, 6.2832f);
      Poly(v, {7.5f, 4, 6, 0, 10.5f, 3});
      Poly(v, {20.5f, 4, 22, 0, 17.5f, 3});
      Ell(v, 10, 11, 4, 4); Ell(v, 18, 11, 4, 4);
      Dot(v, 10, 11); Dot(v, 18, 11);
      Poly(v, {14, 13, 12, 16, 16, 16, 14, 13});
      Poly(v, {7, 17, 9, 25}); Poly(v, {21, 17, 19, 25});
      Poly(v, {10, 31, 9, 34}); Poly(v, {13, 31, 13, 34});
      Poly(v, {16, 31, 16, 34}); Poly(v, {19, 31, 20, 34});
      Line(v, -2, 34, 30, 34);
      return "HOOT";
    case HEART: {
      Stroke st;
      for (int i = 0; i <= 40; ++i) {
        const float a = i * 0.15708f, s = sinf(a);
        const float hx = 16 * s * s * s;
        const float hy = 13 * cosf(a) - 5 * cosf(2 * a) - 2 * cosf(3 * a) - cosf(4 * a);
        st.p.push_back({14 + hx * 0.78f, 17 - hy * 0.85f});
      }
      v.push_back(st);
      return "LOVE"; }
    case BULB:
      Ell(v, 13, 11, 9, 9.5f);
      Poly(v, {8, 19, 8, 25, 18, 25, 18, 19});
      Line(v, 8, 21, 18, 21); Line(v, 8, 23, 18, 23);
      Poly(v, {10, 11, 11, 15, 13, 10, 15, 15, 16, 11});
      for (int i = 0; i < 5; ++i) {
        const float a = -2.62f + i * 0.655f;
        Line(v, 13 + cosf(a) * 12, 11 + sinf(a) * 12,
                13 + cosf(a) * 16, 11 + sinf(a) * 16);
      }
      return "IDEA";
    case CUPCAKE:
      Poly(v, {5, 17, 8, 31, 22, 31, 25, 17, 5, 17});
      Line(v, 11, 17, 12, 31); Line(v, 15, 17, 15, 31); Line(v, 19, 17, 18, 31);
      Poly(v, {4, 17, 5, 11, 10, 14, 12, 7, 17, 12, 21, 8, 24, 14, 26, 17});
      Ell(v, 14, 5, 2.6f, 2.6f);
      Poly(v, {14, 2.5f, 16, -1});
      return "YUM";
    case TRAIN:
      Poly(v, {2, 10, 2, 24, 14, 24, 14, 10, 2, 10});
      Poly(v, {5, 13, 5, 18, 11, 18, 11, 13, 5, 13});
      Poly(v, {14, 14, 14, 24, 33, 24, 33, 14, 14, 14});
      Ell(v, 33, 19, 5, 5, 4.712f, 7.854f);
      Poly(v, {25, 14, 25, 8, 31, 8, 31, 14});
      Ell(v, 28, 4, 3.2f, 2.6f); Ell(v, 23, 0, 4, 3.2f); Ell(v, 16, -3, 3, 2.4f);
      Ell(v, 7, 27, 3.4f, 3.4f); Ell(v, 18, 27, 3.4f, 3.4f); Ell(v, 28, 27, 3.4f, 3.4f);
      Line(v, -2, 31, 38, 31);
      return "CHOO";
    case UMBRELLA:
      Ell(v, 15, 13, 13.5f, 9.5f, 3.1416f, 6.2832f);
      Poly(v, {1.5f, 13, 6, 16, 10.5f, 13, 15, 16, 19.5f, 13, 24, 16, 28.5f, 13});
      Line(v, 15, 3.5f, 15, 28);
      Ell(v, 11, 28, 4, 4, 0, 3.1416f);
      Line(v, 15, 4, 6, 13.5f); Line(v, 15, 4, 24, 13.5f);
      return "";
    case MOON:
      Ell(v, 13, 14, 11, 11, 1.15f, 5.13f);
      Ell(v, 17, 14, 9, 10.5f, 1.45f, 4.83f);
      Poly(v, {27, 3, 29, 5}); Poly(v, {29, 3, 27, 5});
      Poly(v, {2, 25, 4, 27}); Poly(v, {4, 25, 2, 27});
      Poly(v, {26, 24, 28, 26}); Poly(v, {28, 24, 26, 26});
      return "";
    case DOG:
      Ell(v, 12, 10, 7, 6);
      Poly(v, {5.5f, 6, 1, 13, 6, 16});
      Poly(v, {18.5f, 6, 23, 13, 18, 16});
      Dot(v, 9.5f, 9); Dot(v, 14.5f, 9);
      Ell(v, 12, 13, 1.8f, 1.4f);
      Poly(v, {12, 14.5f, 12, 16});
      Poly(v, {8.5f, 17, 12, 16, 15.5f, 17});
      Poly(v, {6, 16, 4, 28, 21, 28, 19, 16});
      Poly(v, {7, 28, 7, 32}); Poly(v, {11, 28, 11, 32});
      Poly(v, {15, 28, 15, 32}); Poly(v, {19, 28, 19, 32});
      Poly(v, {21, 24, 27, 20, 25, 27});
      Line(v, 1, 32, 30, 32);
      return "WOOF";
    default:
      return "";
    }
  }

  // Text-only slates: the sort of thing that ends up between the pictures.
  static const char *Phrase(int i) {
    static const char *kPhrases[] = {
      "2+2=4", "HELLO", "E=MC2", "A+", "OK!", "CHALK", "TODAY:", "WOW!",
      "3.14159", "HA HA", "X=Y+1", "PS.", "THE END", "HI THERE",
    };
    return kPhrases[i % (int)(sizeof(kPhrases) / sizeof(kPhrases[0]))];
  }

  // ---- doodle construction -------------------------------------------------
  static void BBox(const std::vector<Stroke> &v, float *mnx, float *mny,
                   float *mxx, float *mxy) {
    *mnx = *mny = 1e9f; *mxx = *mxy = -1e9f;
    for (const Stroke &st : v)
      for (const Pt &p : st.p) {
        if (p.x < *mnx) *mnx = p.x;
        if (p.x > *mxx) *mxx = p.x;
        if (p.y < *mny) *mny = p.y;
        if (p.y > *mxy) *mxy = p.y;
      }
    if (*mnx > *mxx) { *mnx = *mny = 0; *mxx = *mxy = 1; }
  }

  int NextKind() {
    if (bag_.empty()) {                       // shuffled bag: no near repeats
      for (int i = 0; i < NKIND; ++i) bag_.push_back(i);
      for (int i = (int)bag_.size(); i > 1; --i) std::swap(bag_[i - 1], bag_[rand() % i]);
    }
    const int k = bag_.back();
    bag_.pop_back();
    return k;
  }

  void MakeDoodle(Doodle &d, int kind, float boxH) {
    std::vector<Stroke> raw;
    if (kind == WORDS) {
      const char *txt = Phrase(rand());
      Text(raw, txt, 0, 0, 11.0f);
      float a, b, e, f;
      BBox(raw, &a, &b, &e, &f);
      Stroke ul;                              // hand-drawn underline squiggle
      for (int i = 0; i <= 10; ++i)
        ul.p.push_back({a + (e - a) * i / 10.0f, f + 4.0f + ((i & 1) ? 0.9f : -0.9f)});
      raw.push_back(ul);
    } else {
      const char *label = BuildArt(kind, raw);
      if (label && *label && (rand() % 100) < 55) {
        float a, b, e, f;
        BBox(raw, &a, &b, &e, &f);
        const float sz = 7.0f;
        Text(raw, label, (a + e) * 0.5f - TextW(label, sz / 4.0f) * 0.5f, f + 5.0f, sz);
      }
    }

    // Resample into short pieces and jitter them, once, so the strokes keep a
    // steady hand-drawn wobble instead of shimmering every frame.
    uint32_t seed = (uint32_t)d.id * 2654435761u + 12345u;
    for (const Stroke &st : raw) {
      Stroke out;
      if (st.p.size() < 2) {
        out.p = st.p;
      } else {
        out.p.push_back(st.p[0]);
        for (size_t i = 1; i < st.p.size(); ++i) {
          const Pt a = st.p[i - 1], b = st.p[i];
          const int n = std::max(1, (int)(hypotf(b.x - a.x, b.y - a.y) / 1.7f));
          for (int k = 1; k <= n; ++k) {
            const float u = (float)k / n;
            out.p.push_back({a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u});
          }
        }
      }
      for (Pt &p : out.p) {
        seed = seed * 1664525u + 1013904223u;
        p.x += ((int)((seed >> 13) & 63) - 31) * 0.0135f;
        seed = seed * 1664525u + 1013904223u;
        p.y += ((int)((seed >> 13) & 63) - 31) * 0.0135f;
      }
      d.s.push_back(out);
    }

    // Tilt the whole piece a little — nobody squares a doodle up to the board.
    {
      const float ang = (frand() - 0.5f) * (kind == WORDS ? 0.26f : 0.44f);
      const float ca = cosf(ang), sa = sinf(ang);
      float a0, b0, e0, f0;
      BBox(d.s, &a0, &b0, &e0, &f0);
      const float ox = (a0 + e0) * 0.5f, oy = (b0 + f0) * 0.5f;
      for (Stroke &st : d.s)
        for (Pt &p : st.p) {
          const float dx = p.x - ox, dy = p.y - oy;
          p.x = ox + dx * ca - dy * sa;
          p.y = oy + dx * sa + dy * ca;
        }
    }

    float mnx, mny, mxx, mxy;
    BBox(d.s, &mnx, &mny, &mxx, &mxy);
    const float cy = (mny + mxy) * 0.5f;
    for (Stroke &st : d.s)
      for (Pt &p : st.p) { p.x -= mnx; p.y -= cy; }

    d.scale = boxH / std::max(6.0f, mxy - mny);
    d.scale = std::max(0.85f, std::min(2.4f, d.scale));
    d.w = (mxx - mnx) * d.scale;
    d.total = 0.0f;
    for (const Stroke &st : d.s) {
      if (st.p.size() < 2) { d.total += kDotLen_; continue; }
      for (size_t i = 1; i < st.p.size(); ++i)
        d.total += hypotf(st.p[i].x - st.p[i - 1].x, st.p[i].y - st.p[i - 1].y) * d.scale;
    }
    d.drawn = 0.0f;
    // Pace the chalk so a doodle takes about as long to draw as the board takes
    // to slide its own width past — the tip then stays near the right-hand edge,
    // laying line art down onto the slate just as it swings into view.
    d.rate = d.total * kScroll_ * (d.tall ? 1 : nlane_) / std::max(10.0f, d.w + kGap_);
    d.rate = std::max(22.0f, std::min(170.0f, d.rate));
  }

  void Spawn() {
    if (activeId_ >= 0) return;                       // one hand, one doodle
    if (pend_.empty()) {                              // rough out the next piece
      Doodle d;
      d.id = nextId_++;
      d.lane = (nlane_ == 2 && front_[1] < front_[0]) ? 1 : 0;
      d.tall = (nlane_ == 2 && (rand() % 100) < 16 &&
                fabsf(front_[0] - front_[1]) < 14.0f);
      const int kind = NextKind();
      MakeDoodle(d, kind, (d.tall ? laneH_ * 2.15f : laneH_) *
                          (kind == WORDS ? 0.62f : 1.0f));
      Tint(d);
      pend_.push_back(d);
    }
    Doodle &d = pend_[0];
    const float start = d.tall ? std::max(front_[0], front_[1]) : front_[d.lane];
    // Never draw off the edge: wait until the whole piece fits in view, so
    // every stroke is laid down where it can be watched.
    if (d.w < width_ - 2.0f && start + d.w > cam_ + width_) return;
    d.wx = start;
    d.cy = (d.tall ? trayY_ * 0.5f : lane_[d.lane]) + ((rand() % 9) - 4) * 0.5f;
    if (d.tall) front_[0] = front_[1] = start + d.w + kGap_;
    else        front_[d.lane] = start + d.w + kGap_;
    activeId_ = d.id;
    doodles_.push_back(d);
    pend_.clear();
  }

  // Come up on an already-full board: lay down finished doodles until the
  // frontier is past the right edge, then let the hand take over live.
  void Prefill() {
    for (int i = 0; i < 80; ++i) {
      const size_t before = doodles_.size();
      Spawn();
      if (doodles_.size() == before) break;
      doodles_.back().drawn = doodles_.back().total;
      activeId_ = -1;
    }
  }

  static void Tint(Doodle &d) {
    static const uint8_t kTint[][3] = {
      {236, 240, 232}, {236, 240, 232}, {236, 240, 232}, {236, 240, 232},
      {244, 228, 152}, {246, 188, 198}, {184, 216, 246}, {192, 240, 198},
    };
    const int i = rand() % (int)(sizeof(kTint) / sizeof(kTint[0]));
    d.r = kTint[i][0]; d.g = kTint[i][1]; d.b = kTint[i][2];
  }

  void Advance(float dt) {
    // Speed the hand up when the drawn edge is falling behind the right edge of
    // the board, so the slate stays full no matter how long a doodle runs.
    const float lag = (cam_ + width_) - std::min(front_[0], front_[nlane_ - 1]);
    const float boost = 1.0f + std::max(0.0f, std::min(1.8f, lag / 55.0f));
    for (Doodle &d : doodles_) {
      if (d.id != activeId_) continue;
      d.drawn += d.rate * boost * dt;
      if (d.drawn >= d.total) { d.drawn = d.total; activeId_ = -1; }
      break;
    }
    while (!doodles_.empty() && doodles_.front().wx + doodles_.front().w < cam_ - 3.0f)
      doodles_.erase(doodles_.begin());
  }

  // ---- rendering -----------------------------------------------------------
  inline void ChalkPt(Canvas *c, float sx, float sy, const Doodle &d) {
    const int x = (int)lroundf(sx), y = (int)lroundf(sy);
    if (y >= trayY_) return;                          // chalk stops at the tray
    const uint32_t h = Hash2(x + (int)cam_, y);
    if ((h & 15) == 0) return;                        // dusty gaps in the line
    const float g = 0.60f + 0.40f * ((h >> 9) & 255) / 255.0f;
    Px(c, x, y, (int)(d.r * g), (int)(d.g * g), (int)(d.b * g));
  }

  void DrawDoodle(Canvas *c, const Doodle &d) {
    const float sx0 = d.wx - cam_;
    if (sx0 > width_ + 2.0f || sx0 + d.w < -2.0f) return;
    float budget = d.drawn;
    for (const Stroke &st : d.s) {
      if (budget <= 0.0f) return;
      if (st.p.size() < 2) {
        ChalkPt(c, sx0 + st.p[0].x * d.scale, d.cy + st.p[0].y * d.scale, d);
        budget -= kDotLen_;
        continue;
      }
      for (size_t i = 1; i < st.p.size(); ++i) {
        const float ax = sx0 + st.p[i - 1].x * d.scale, ay = d.cy + st.p[i - 1].y * d.scale;
        const float bx = sx0 + st.p[i].x * d.scale,     by = d.cy + st.p[i].y * d.scale;
        const float len = hypotf(bx - ax, by - ay);
        if (len < 0.0001f) continue;
        const float f = (budget < len) ? budget / len : 1.0f;
        const int n = std::max(1, (int)(len * f * 2.0f));
        for (int k = 0; k <= n; ++k) {
          const float u = f * k / n;
          ChalkPt(c, ax + (bx - ax) * u, ay + (by - ay) * u, d);
        }
        budget -= len;
        if (budget <= 0.0f) {                         // this is the chalk tip
          if (d.drawn < d.total) {
            headX_ = ax + (bx - ax) * f;
            headY_ = ay + (by - ay) * f;
            SpawnDust();
          }
          return;
        }
      }
    }
  }

  void DrawChalkStick(Canvas *c, float t) {
    const float jx = sinf(t * 21.0f) * 0.5f, jy = cosf(t * 17.0f) * 0.5f;
    const int x0 = (int)(headX_ + jx), y0 = (int)(headY_ + jy);
    DrawLine(c, x0, y0, x0 + 3, y0 - 5, 246, 240, 226);          // the stick
    DrawLine(c, x0 + 1, y0, x0 + 4, y0 - 5, 208, 200, 182);      // its shaded side
    DrawLine(c, x0 + 3, y0 - 6, x0 + 6, y0 - 10, 150, 128, 104); // fingers, barely
    Px(c, x0, y0, 255, 255, 252);                                // the tip itself
  }

  void SpawnDust() {
    if (dust_.size() >= 70 || (rand() % 100) >= 22) return;
    Dust p;
    p.x = headX_; p.y = headY_ + 1.0f;
    p.vx = -kScroll_ + (frand() - 0.5f) * 6.0f;
    p.vy = frand() * 6.0f;
    p.max = p.life = 1.6f + frand() * 1.8f;
    dust_.push_back(p);
  }
  void UpdateDust(float dt) {
    for (size_t i = 0; i < dust_.size();) {
      Dust &p = dust_[i];
      p.vy += 26.0f * dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.life -= dt;
      if (p.life <= 0.0f || p.y >= trayY_ - 1 || p.x < -2.0f) dust_.erase(dust_.begin() + i);
      else ++i;
    }
  }
  void DrawDust(Canvas *c) {
    for (const Dust &p : dust_) {
      const float a = std::min(1.0f, p.life / p.max) * 0.85f;
      Px(c, (int)p.x, (int)p.y, (int)(210 * a), (int)(216 * a), (int)(208 * a));
    }
  }

  // Value-noise smudge field, cached in board space and scrolled a column at a
  // time so the eraser haze stays stuck to the slate as the camera pans.
  static float Noise(int gx, int gy) { return (Hash2(gx, gy) & 1023) * (1.0f / 1023.0f); }
  static float Smudge(int wx, int y) {
    float v = 0.0f, amp = 0.62f, fx = wx / 17.0f, fy = y / 9.0f;
    for (int o = 0; o < 2; ++o) {
      const int ix = (int)floorf(fx), iy = (int)floorf(fy);
      float tx = fx - ix, ty = fy - iy;
      tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty);
      const float a = Noise(ix, iy),     b = Noise(ix + 1, iy);
      const float e = Noise(ix, iy + 1), f = Noise(ix + 1, iy + 1);
      const float top = a + (b - a) * tx, bot = e + (f - e) * tx;
      v += amp * (top + (bot - top) * ty);
      fx *= 2.3f; fy *= 2.1f; amp *= 0.45f;
    }
    return v;
  }
  void FillSmudgeCol(int x, int camx) {
    for (int y = 0; y < trayY_; ++y) {
      float s = Smudge(x + camx, y);
      s += 0.28f * std::max(0.0f, (y - trayY_ * 0.72f) / (trayY_ * 0.28f));  // haze over the tray
      smudge_[(size_t)y * width_ + x] = (uint8_t)clampi((int)(s * 255.0f), 0, 255);
    }
  }
  void RollSmudge() {
    const int camx = (int)cam_;
    const int shift = camx - smudgeCam_;
    if (shift == 0) return;
    if (shift < 0 || shift >= width_) {                 // reset / huge jump
      for (int x = 0; x < width_; ++x) FillSmudgeCol(x, camx);
    } else {
      for (int y = 0; y < trayY_; ++y)
        memmove(&smudge_[(size_t)y * width_], &smudge_[(size_t)y * width_ + shift],
                (size_t)(width_ - shift));
      for (int x = width_ - shift; x < width_; ++x) FillSmudgeCol(x, camx);
    }
    smudgeCam_ = camx;
  }

  void DrawBoard(Canvas *c) {
    const int camx = (int)cam_;
    for (int y = 0; y < trayY_; ++y) {
      for (int x = 0; x < width_; ++x) {
        const int s = smudge_[(size_t)y * width_ + x];
        const int n = (int)(Hash2(x + camx, y * 3 + 1) & 3);
        Px(c, x, y, 6 + (s * 26 >> 8) + n, 21 + (s * 30 >> 8) + n, 15 + (s * 25 >> 8) + n);
      }
    }
  }

  void DrawTray(Canvas *c) {
    static const uint8_t kWood[5][3] = {
      {104, 72, 40}, {142, 100, 58}, {116, 80, 46}, {78, 53, 30}, {50, 34, 19}};
    for (int i = 0; i < 5 && trayY_ + i < height_; ++i)
      for (int x = 0; x < width_; ++x)
        Px(c, x, trayY_ + i, kWood[i][0], kWood[i][1], kWood[i][2]);
    // Sticks of chalk and a felt eraser resting on the ledge.
    static const struct { float at; int len; uint8_t r, g, b; } kSticks[] = {
      {0.13f, 9, 238, 240, 232}, {0.31f, 7, 246, 190, 200},
      {0.68f, 10, 244, 230, 158}, {0.86f, 6, 190, 220, 246}};
    for (const auto &s : kSticks) {
      const int x0 = (int)(width_ * s.at);
      for (int x = x0; x < x0 + s.len; ++x) {
        Px(c, x, trayY_, s.r, s.g, s.b);
        Px(c, x, trayY_ - 1, s.r * 3 / 4, s.g * 3 / 4, s.b * 3 / 4);
      }
    }
    const int ex = (int)(width_ * 0.47f);
    for (int x = ex; x < ex + 15 && x < width_; ++x) {
      Px(c, x, trayY_ - 3, 168, 126, 72);
      Px(c, x, trayY_ - 2, 140, 102, 58);
      Px(c, x, trayY_ - 1, 74, 74, 84);
      Px(c, x, trayY_, 56, 56, 66);
    }
  }
};

REGISTER_MODE(27, BlackboardMode);
