// StockTickerMode -- registered as mode 33.
#include "common/mode.h"
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>

// --- 34: Stock ticker -----------------------------------------------------
// Plots an intraday stock chart. Change the symbol over telnet by passing it
// as an argument to the mode number, e.g. "34 TSLA". A background thread
// re-fetches every 20s via curl (Yahoo Finance chart API). range=1d returns
// the latest trading session, so after hours / weekends it shows the previous
// day's data; a 5d fallback covers empty responses.
class StockTickerMode : public Mode {
public:
  const char *name() const override { return "Stock Ticker"; }
  const char *DefaultArg() const override { return "SPY"; }
  bool TakesArg() const override { return true; }
  const char *ArgHint() const override { return "symbol, e.g. TSLA"; }
  const char *ArgUsage() const override {
    return "One ticker symbol: TSLA, AAPL, ^GSPC, BRK-B. Defaults to SPY.";
  }
  void SetArg(const std::string &arg) override {
    std::string s;
    for (char c : arg) {
      if (c >= 'a' && c <= 'z') c -= 32;
      if ((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='^') s += c;
      if (s.size() >= 12) break;
    }
    if (s.empty()) return;
    std::lock_guard<std::mutex> lk(mtx_);
    symbol_ = s;
    prices_.clear();
    status_ = "loading " + s;
    dirty_.store(true);
  }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    if (!fontTried_) {
      fontTried_ = true;
      const char *cands[] = { "fonts/6x10.bdf", "fonts/5x7.bdf",
                              "/root/led-matrix-server/fonts/6x10.bdf" };
      for (const char *p : cands) if (font_.LoadFont(p)) { fontOK_ = true; break; }
    }
    if (!started_) {
      started_ = true;
      std::thread(&StockTickerMode::FetchLoop, this).detach();
    }
  }
  void Draw(Canvas *c, float t, float dt) override {
    std::vector<float> px; float prev, last; std::string sym, st;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      px = prices_; prev = prevClose_; last = last_; sym = symbol_; st = status_;
    }
    c->Fill(0, 0, 6);
    const int W = width_, H = height_;
    const int top = (fontOK_ ? font_.height() + 1 : 10);
    const int bot = H - 1;
    const bool up = last >= prev;
    const rgb_matrix::Color line = up ? rgb_matrix::Color(40, 220, 80)
                                      : rgb_matrix::Color(235, 60, 60);
    if (fontOK_) {
      char hdr[80];
      if (px.size() >= 2 && prev > 0)
        snprintf(hdr, sizeof hdr, "%s %.2f %+.1f%%", sym.c_str(), last,
                 (last - prev) / prev * 100.0f);
      else
        snprintf(hdr, sizeof hdr, "%s %s", sym.c_str(), st.c_str());
      rgb_matrix::DrawText(c, font_, 1, font_.baseline(),
                           rgb_matrix::Color(235, 235, 235), hdr);
    }
    if (px.size() < 2) return;
    float mn = px[0], mx = px[0];
    for (float v : px) { if (v < mn) mn = v; if (v > mx) mx = v; }
    if (prev > 0) { if (prev < mn) mn = prev; if (prev > mx) mx = prev; }
    float range = mx - mn; if (range < 1e-6f) range = 1.0f;
    const int n = (int)px.size();
    int px0 = -1, py0 = -1;
    if (prev > 0) {                              // previous-close baseline (dashed)
      const int yb = bot - (int)((prev - mn) / range * (bot - top));
      for (int x = 0; x < W; x += 4) c->SetPixel(x, yb, 70, 70, 95);
    }
    for (int i = 0; i < n; ++i) {
      const int x = (int)((float)i / (n - 1) * (W - 1));
      const int y = clampi(bot - (int)((px[i] - mn) / range * (bot - top)), top, bot);
      for (int yy = y; yy <= bot; ++yy)          // faint area fill
        c->SetPixel(x, yy, line.r / 6, line.g / 6, line.b / 6);
      if (px0 >= 0) rgb_matrix::DrawLine(c, px0, py0, x, y, line);
      else c->SetPixel(x, y, line.r, line.g, line.b);
      px0 = x; py0 = y;
    }
  }
private:
  void FetchLoop() {
    while (g_running.load()) {
      std::string sym;
      { std::lock_guard<std::mutex> lk(mtx_); sym = symbol_; dirty_.store(false); }
      std::vector<float> px; float prev = 0, last = 0; std::string cur;
      const bool ok = Fetch(sym, px, prev, last, cur);
      {
        std::lock_guard<std::mutex> lk(mtx_);
        if (sym == symbol_) {
          if (ok && px.size() >= 2) {
            prices_.swap(px); prevClose_ = prev; last_ = last; status_ = cur;
          } else if (prices_.empty()) {
            status_ = "no data";
          }
        }
      }
      for (int i = 0; i < 200 && g_running.load(); ++i) {   // ~20s, wake on change
        if (dirty_.load()) break;
        usleep(100000);
      }
    }
  }
  bool Fetch(const std::string &sym, std::vector<float> &px,
             float &prev, float &last, std::string &cur) {
    std::string body;
    if (!Http(sym, "1d", "5m", body)) return false;
    ParseMeta(body, prev, last, cur);
    ParseCloses(body, px);
    if (px.size() < 3) {                          // after-hours / weekend fallback
      std::string body2;
      if (Http(sym, "5d", "30m", body2)) {
        ParseMeta(body2, prev, last, cur);
        px.clear(); ParseCloses(body2, px);
      }
    }
    if (!px.empty()) {
      if (last == 0) last = px.back();
      if (prev == 0) prev = px.front();
    }
    return !px.empty();
  }
  bool Http(const std::string &sym, const char *range, const char *interval,
            std::string &out) {
    const std::string cmd =
      "curl -s -m 8 -A 'Mozilla/5.0' "
      "'https://query1.finance.yahoo.com/v8/finance/chart/" + sym +
      "?range=" + range + "&interval=" + interval + "'";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[8192]; size_t k;
    out.clear();
    while ((k = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, k);
    pclose(p);
    return out.find("\"close\"") != std::string::npos;
  }
  static void ParseMeta(const std::string &b, float &prev, float &last,
                        std::string &cur) {
    size_t p;
    if ((p = b.find("\"regularMarketPrice\":")) != std::string::npos)
      last = strtof(b.c_str() + p + 21, nullptr);
    if ((p = b.find("\"chartPreviousClose\":")) != std::string::npos)
      prev = strtof(b.c_str() + p + 21, nullptr);
    else if ((p = b.find("\"previousClose\":")) != std::string::npos)
      prev = strtof(b.c_str() + p + 16, nullptr);
    if ((p = b.find("\"currency\":\"")) != std::string::npos) {
      const size_t s = p + 12, e = b.find('"', s);
      if (e != std::string::npos) cur = b.substr(s, e - s);
    }
  }
  static void ParseCloses(const std::string &b, std::vector<float> &px) {
    size_t p = b.find("\"close\":[");
    if (p == std::string::npos) return;
    p += 9;
    const size_t e = b.find(']', p);
    if (e == std::string::npos) return;
    size_t i = p;
    while (i < e) {
      while (i < e && (b[i] == ',' || b[i] == ' ')) ++i;
      if (i >= e) break;
      if (b.compare(i, 4, "null") == 0) { i += 4; continue; }
      char *end = nullptr;
      const float v = strtof(b.c_str() + i, &end);
      if (end && end > b.c_str() + i) { px.push_back(v); i = end - b.c_str(); }
      else ++i;
    }
  }
  std::mutex mtx_;
  std::string symbol_ = "AAPL";
  std::string status_ = "loading";
  std::vector<float> prices_;
  float prevClose_ = 0, last_ = 0;
  std::atomic<bool> dirty_{false};
  bool started_ = false, fontTried_ = false, fontOK_ = false;
  rgb_matrix::Font font_;
};

REGISTER_MODE(32, StockTickerMode);
