// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// panel-cal: an interactive per-panel brightness calibration tool for a wall of
// LED matrix panels. Fills every panel with a solid test color scaled by that
// panel's brightness, so you can dial a brighter batch down to match the rest.
//
// Controls (over an SSH terminal):
//   Arrow keys : move the active-panel selection (shown with an outlined border)
//   + / -      : trim the active panel, on the channel(s) of the current test
//                color: RED->red, GREEN->green, BLUE->blue, WHITE->all three,
//                CYAN->green+blue, etc. (5% steps)
//   Space      : cycle the test color: RED GREEN BLUE CYAN MAGENTA YELLOW WHITE
//   ESC / q    : exit and save
//
// On exit the per-panel R G B gains (one line per panel, row-major) are written
// to ~/led-matrix-server/panels.cal and printed.
//
// This code is public domain (the led-matrix library it uses is GPL v2).

#include "led-matrix.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

using rgb_matrix::RGBMatrix;
using rgb_matrix::FrameCanvas;

static volatile bool interrupted = false;
static void OnInt(int) { interrupted = true; }

struct TestColor { uint8_t r, g, b; const char *name; };
static const TestColor kColors[] = {
  {255, 0, 0, "RED"},   {0, 255, 0, "GREEN"},  {0, 0, 255, "BLUE"},
  {0, 255, 255, "CYAN"},{255, 0, 255, "MAGENTA"},{255, 255, 0, "YELLOW"},
  {255, 255, 255, "WHITE"},
};
static const int kNumColors = 7;

struct Gain { int r, g, b; };   // per-panel per-channel gain, 0..100 %
static int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

// Directory containing this executable (so we can find led-matrix-server.conf next to it).
static std::string ExeDir() {
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
  std::string p(buf);
  const size_t s = p.find_last_of('/');
  return s == std::string::npos ? "." : p.substr(0, s);
}
// Parse the shared led-matrix-server.conf (KEY=VALUE lines) into a map.
static std::map<std::string, std::string> ReadConf(const std::string &path) {
  std::map<std::string, std::string> m;
  FILE *f = fopen(path.c_str(), "r");
  if (!f) return m;
  char line[512];
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
    char *eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    std::string key(p), val(eq + 1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    m[key] = val;
  }
  fclose(f);
  return m;
}
static int ConfInt(const std::map<std::string, std::string> &m, const char *k, int def) {
  auto it = m.find(k);
  return it == m.end() ? def : atoi(it->second.c_str());
}
static bool MatrixModesRunning() { return system("pgrep -x led-matrix-server >/dev/null 2>&1") == 0; }
static void Run(const char *cmd) { int rc = system(cmd); (void)rc; }

int main(int argc, char **argv) {
  if (geteuid() != 0) {
    fprintf(stderr, "panel-cal needs root (GPIO + service control). Try: sudo %s\n", argv[0]);
    return 1;
  }
  // Never allow a second instance -- two processes on the GPIO/DMA hard-lock
  // the Pi. (pgrep counts us, so >1 means another calibrator is already up.)
  if (system("[ \"$(pgrep -xc panel-cal)\" -gt 1 ]") == 0) {
    fprintf(stderr, "Another panel-cal is already running. Close that one first.\n");
    return 1;
  }

  // Configure from the SAME led-matrix-server.conf the main app uses (found next to us).
  const std::string confPath = ExeDir() + "/led-matrix-server.conf";
  const std::map<std::string, std::string> cfg = ReadConf(confPath);
  RGBMatrix::Options opt;
  rgb_matrix::RuntimeOptions ropt;
  opt.rows = ConfInt(cfg, "LED_ROWS", 64);
  opt.cols = ConfInt(cfg, "LED_COLS", 64);
  opt.chain_length = ConfInt(cfg, "LED_CHAIN", 3);
  opt.parallel = ConfInt(cfg, "LED_PARALLEL", 2);
  opt.brightness = ConfInt(cfg, "CAL_BRIGHTNESS", 20);   // PSU current cap
  opt.hardware_mapping = "regular";
  ropt.gpio_slowdown = ConfInt(cfg, "LED_SLOWDOWN_GPIO", 3);
  ropt.drop_privileges = 0;

  // Two processes on the GPIO/DMA hard-lock the Pi, so take over from the main
  // service: stop it (wait until really gone) and restart it when we exit.
  bool restartService = false;
  if (MatrixModesRunning()) {
    printf("Stopping led-matrix-server service...\n"); fflush(stdout);
    if (system("systemctl stop led-matrix-server >/dev/null 2>&1") != 0) { /* keep going, verify below */ }
    for (int i = 0; i < 40 && MatrixModesRunning(); ++i) usleep(250000);
    if (MatrixModesRunning()) { fprintf(stderr, "Couldn't stop led-matrix-server; aborting.\n"); return 1; }
    restartService = true;
  }

  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(opt, ropt);
  if (matrix == NULL) { if (restartService) Run("systemctl start led-matrix-server >/dev/null 2>&1"); return 1; }
  signal(SIGTERM, OnInt);
  signal(SIGINT, OnInt);

  const int panelW = opt.cols, panelH = opt.rows;
  const int gcols = opt.chain_length, grows = opt.parallel;
  const int N = gcols * grows;
  std::vector<Gain> gain(N, Gain{100, 100, 100});

  const std::string home = getenv("HOME") ? getenv("HOME") : "/root";
  const std::string calpath = home + "/led-matrix-server/panels.cal";
  if (FILE *f = fopen(calpath.c_str(), "r")) {   // resume: one "R G B" line per panel
    char line[256]; int i = 0;
    while (i < N && fgets(line, sizeof line, f)) {
      if (line[0] == '#') continue;
      int r, g, b;
      if (sscanf(line, "%d %d %d", &r, &g, &b) == 3) gain[i++] = Gain{Clamp100(r), Clamp100(g), Clamp100(b)};
    }
    fclose(f);
  }
  int sel = 0, colorIdx = kNumColors - 1;   // start on WHITE

  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  newt.c_cc[VMIN] = 0; newt.c_cc[VTIME] = 1;   // 0.1s read timeout
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  auto printState = [&]() {
    const TestColor &c = kColors[colorIdx];
    char chans[4] = {0}; int ci = 0;
    if (c.r) chans[ci++] = 'R';
    if (c.g) chans[ci++] = 'G';
    if (c.b) chans[ci++] = 'B';
    printf("\r\033[K%-7s panel %d (r%d,c%d)  RGB=%d,%d,%d   +/- trims %-3s   "
           "arrows=panel space=color ESC=save",
           c.name, sel + 1, sel / gcols, sel % gcols,
           gain[sel].r, gain[sel].g, gain[sel].b, chans);
    fflush(stdout);
  };
  printf("panel-cal: %dx%d panels in a %dx%d grid. ", panelW, panelH, gcols, grows);
  printState();

  FrameCanvas *fc = matrix->CreateFrameCanvas();
  bool running = true;
  while (running && !interrupted) {
    const TestColor c = kColors[colorIdx];
    for (int py = 0; py < grows; ++py)
      for (int px = 0; px < gcols; ++px) {
        const int idx = py * gcols + px;
        const Gain &g = gain[idx];
        const uint8_t rr = (uint8_t)(c.r * g.r / 100), gg = (uint8_t)(c.g * g.g / 100),
                      bb = (uint8_t)(c.b * g.b / 100);
        const int x0 = px * panelW, y0 = py * panelH;
        for (int y = 0; y < panelH; ++y)
          for (int x = 0; x < panelW; ++x) fc->SetPixel(x0 + x, y0 + y, rr, gg, bb);
      }
    // Outline the active panel in the inverse of the test color (always visible).
    {
      const int px = sel % gcols, py = sel / gcols, x0 = px * panelW, y0 = py * panelH;
      const uint8_t ir = 255 - c.r, ig = 255 - c.g, ib = 255 - c.b;
      for (int x = 0; x < panelW; ++x) {
        fc->SetPixel(x0 + x, y0, ir, ig, ib);
        fc->SetPixel(x0 + x, y0 + panelH - 1, ir, ig, ib);
      }
      for (int y = 0; y < panelH; ++y) {
        fc->SetPixel(x0, y0 + y, ir, ig, ib);
        fc->SetPixel(x0 + panelW - 1, y0 + y, ir, ig, ib);
      }
    }
    fc = matrix->SwapOnVSync(fc);

    unsigned char ch;
    if (read(STDIN_FILENO, &ch, 1) <= 0) continue;   // timeout, keep displaying
    if (ch == 27) {                                   // ESC or an arrow sequence
      unsigned char a;
      if (read(STDIN_FILENO, &a, 1) <= 0) { running = false; break; }  // lone ESC
      if (a == '[') {
        unsigned char k;
        if (read(STDIN_FILENO, &k, 1) > 0) {
          int col = sel % gcols, row = sel / gcols;
          if (k == 'A' && row > 0) row--;
          else if (k == 'B' && row < grows - 1) row++;
          else if (k == 'C' && col < gcols - 1) col++;
          else if (k == 'D' && col > 0) col--;
          sel = row * gcols + col;
          printState();
        }
      }
    } else if (ch == '+' || ch == '=' || ch == '-' || ch == '_') {
      const int d = (ch == '+' || ch == '=') ? 5 : -5;   // trim the current color's channels
      const TestColor &cc = kColors[colorIdx];
      Gain &g = gain[sel];
      if (cc.r) g.r = Clamp100(g.r + d);
      if (cc.g) g.g = Clamp100(g.g + d);
      if (cc.b) g.b = Clamp100(g.b + d);
      printState();
    }
    else if (ch == ' ') { colorIdx = (colorIdx + 1) % kNumColors; printState(); }
    else if (ch == 'q' || ch == 'Q') { running = false; }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  matrix->Clear();
  delete matrix;

  if (FILE *f = fopen(calpath.c_str(), "w")) {
    fprintf(f, "# per-panel R G B gain %%, one line per panel, row-major "
               "(left-to-right, top-to-bottom)\n");
    for (int i = 0; i < N; ++i) fprintf(f, "%d %d %d\n", gain[i].r, gain[i].g, gain[i].b);
    fclose(f);
  }
  printf("\nSaved %s:\n", calpath.c_str());
  for (int i = 0; i < N; ++i)
    printf("  panel %d: R=%d G=%d B=%d\n", i + 1, gain[i].r, gain[i].g, gain[i].b);

  if (restartService) {
    printf("Restarting led-matrix-server service...\n");
    Run("systemctl start led-matrix-server >/dev/null 2>&1");
  }
  return 0;
}
