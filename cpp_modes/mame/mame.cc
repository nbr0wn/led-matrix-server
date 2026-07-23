// MameMode -- registered as mode 35.
#include "common/mode.h"
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/input.h>
#include <dirent.h>

// --- 36: MAME arcade launcher ---------------------------------------------
// A controller-driven front end: a scrollable list of arcade games rendered on
// the matrix and navigated with the gamepad. Selecting one launches MAME inside
// a panel-sized virtual X screen and blits its framebuffer;
// Back+Start on the pad quits back to the menu. The pad is read via Linux evdev.
class MameMode : public Mode {
public:
  const char *name() const override { return "MAME"; }
  ~MameMode() override { if (playing_) StopGame(); }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    if (!fontTried_) {
      fontTried_ = true;
      const char *cands[] = { "fonts/6x10.bdf", "fonts/5x7.bdf",
                              "/root/led-matrix-server/fonts/6x10.bdf" };
      for (const char *p : cands) if (font_.LoadFont(p)) { fontOK_ = true; break; }
    }
    if (padfd_ < 0) padfd_ = OpenPad();
    if (games_.empty()) BuildGameList();
  }
  void Draw(Canvas *c, float t, float dt) override {
    if (playing_) DrawPlaying(c); else DrawMenu(c);
  }
private:
  struct Game { const char *rom, *title; };

  int OpenPad() {
    for (int i = 0; i < 32; ++i) {
      char path[32]; snprintf(path, sizeof path, "/dev/input/event%d", i);
      const int fd = open(path, O_RDONLY | O_NONBLOCK);
      if (fd < 0) continue;
      char nm[256] = {0};
      if (ioctl(fd, EVIOCGNAME(sizeof nm), nm) >= 0) {
        const std::string n(nm);
        if (n.find("Dual Action") != std::string::npos ||
            n.find("Gamepad") != std::string::npos ||
            n.find("Logitech") != std::string::npos ||
            n.find("Xbox") != std::string::npos || n.find("X-Box") != std::string::npos)
          return fd;
      }
      close(fd);
    }
    return open("/dev/input/event4", O_RDONLY | O_NONBLOCK);  // fallback
  }
  void BuildGameList() {
    static const Game kAll[] = {
      {"pacman","PAC-MAN"}, {"mspacman","MS. PAC-MAN"}, {"galaga","GALAGA"},
      {"galaxian","GALAXIAN"}, {"dkong","DONKEY KONG"}, {"dkongjr","DONKEY KONG JR"},
      {"invaders","SPACE INVADERS"}, {"digdug","DIG DUG"}, {"frogger","FROGGER"},
      {"pengo","PENGO"}, {"pooyan","POOYAN"}, {"mrdo","MR. DO!"},
      {"mario","MARIO BROS"}, {"joust","JOUST"}, {"bublbobl","BUBBLE BOBBLE"},
      {"qbert","Q*BERT"}, {"btime","BURGERTIME"}, {"kungfum","KUNG-FU MASTER"},
      {"gng","GHOSTS N GOBLINS"}, {"rampage","RAMPAGE"}, {"robotron","ROBOTRON"},
      {"defender","DEFENDER"}, {"blktiger","BLACK TIGER"}, {"elevator","ELEVATOR ACTION"},
      {"1942","1942"}, {"zaxxon","ZAXXON"},
    };
    const std::string home = getenv("HOME") ? getenv("HOME") : "/root";
    games_.clear();
    for (const Game &g : kAll) {
      const std::string zip = home + "/mame-roms/" + g.rom + ".zip";
      if (access(zip.c_str(), F_OK) == 0) games_.push_back(g);
    }
    if (sel_ >= (int)games_.size()) sel_ = 0;
  }

  void PollPad() {
    if (padfd_ < 0) return;
    struct input_event ev;
    while (read(padfd_, &ev, sizeof ev) == (ssize_t)sizeof ev) {
      if (ev.type == EV_ABS) {
        if (ev.code == ABS_HAT0Y) dpadY_ = ev.value;
        else if (ev.code == ABS_Y) stickY_ = ev.value;   // 0..255, center ~128
      } else if (ev.type == EV_KEY) {
        const bool down = ev.value != 0;
        if (ev.code >= 0x120 && ev.code <= 0x123) { if (down) selectEdge_ = true; }
        else if (ev.code == 0x128) back_ = down;          // Back  (BTN_BASE3)
        else if (ev.code == 0x129) start_ = down;         // Start (BTN_BASE4)
      }
    }
  }
  int NavDir() {   // -1 up / +1 down / 0 none, from D-pad or left stick
    if (dpadY_ < 0) return -1;
    if (dpadY_ > 0) return 1;
    if (stickY_ < 64) return -1;
    if (stickY_ > 192) return 1;
    return 0;
  }

  void DrawMenu(Canvas *c) {
    PollPad();
    const int n = (int)games_.size();
    const int dir = NavDir();
    if (dir != 0 && n > 0) {
      if (navCd_ <= 0) { sel_ = (sel_ + dir + n) % n; navCd_ = navFirst_ ? 16 : 6; navFirst_ = false; }
      else navCd_--;
    } else { navCd_ = 0; navFirst_ = true; }
    if (selectEdge_) { selectEdge_ = false; if (n > 0) { LaunchGame(games_[sel_].rom); return; } }

    c->Fill(0, 0, 0);
    if (n == 0) {
      if (fontOK_) rgb_matrix::DrawText(c, font_, 2, height_/2,
                                        rgb_matrix::Color(220,80,80), "no ROMs in ~/mame-roms");
      return;
    }
    const int rowH = fontOK_ ? font_.height() : 8;
    const int visible = height_ / rowH;
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + visible) top_ = sel_ - visible + 1;
    for (int i = 0; i < visible && top_ + i < n; ++i) {
      const int gi = top_ + i, y = i * rowH;
      const bool cur = (gi == sel_);
      if (cur)
        for (int yy = y; yy < y + rowH && yy < height_; ++yy)
          for (int x = 0; x < width_; ++x) c->SetPixel(x, yy, 0, 40, 90);
      if (fontOK_)
        rgb_matrix::DrawText(c, font_, 2, y + font_.baseline(),
                             cur ? rgb_matrix::Color(255,255,255) : rgb_matrix::Color(120,150,180),
                             games_[gi].title);
    }
  }

  // The launcher (once mame-run.sh) is inlined so there is no shell script to
  // deploy: boot a panel-sized virtual X screen with its framebuffer in
  // /tmp/mmfb (which we mmap), then run MAME in it. $1=rom $2=width $3=height.
  static const char *RunScript() {
    return R"SH(
ROM="$1"; W="${2:-192}"; H="${3:-128}"; FBDIR=/tmp/mmfb
pkill -f "Xvfb :99" 2>/dev/null; rm -f /tmp/.X99-lock
rm -rf "$FBDIR"; mkdir -p "$FBDIR"
Xvfb :99 -screen 0 "${W}x${H}x24" -fbdir "$FBDIR" >/tmp/mm-xvfb.log 2>&1 &
XPID=$!; trap 'kill $XPID 2>/dev/null' EXIT
sleep 1.5
export DISPLAY=:99
/usr/games/mame "$ROM" -rompath "$HOME/mame-roms" \
  -video soft -sound none -skip_gameinfo -nomouse -keepaspect >/tmp/mm-mame.log 2>&1
)SH";
  }
  void LaunchGame(const char *rom) {
    char ws[8], hs[8];
    snprintf(ws, sizeof ws, "%d", width_); snprintf(hs, sizeof hs, "%d", height_);
    child_ = fork();
    if (child_ == 0) {
      setsid();                              // own process group, killable wholesale
      const int nul = open("/dev/null", O_RDWR);
      if (nul >= 0) { dup2(nul,0); dup2(nul,1); dup2(nul,2); }
      execl("/bin/sh", "sh", "-c", RunScript(), "mame", rom, ws, hs, (char*)NULL);
      _exit(127);
    }
    playing_ = (child_ > 0);
  }
  void StopGame() {
    if (child_ > 0) {
      killpg(child_, SIGTERM);
      for (int i = 0; i < 40; ++i) { if (waitpid(child_, NULL, WNOHANG) == child_) break; usleep(50000); }
      killpg(child_, SIGKILL); waitpid(child_, NULL, WNOHANG);
    }
    child_ = -1; playing_ = false;
    if (fbmap_) { munmap(fbmap_, fbsize_); fbmap_ = nullptr; }
    if (fbfd_ >= 0) { close(fbfd_); fbfd_ = -1; }
    back_ = start_ = selectEdge_ = false;
  }
  bool TryMapFb() {
    fbfd_ = open("/tmp/mmfb/Xvfb_screen0", O_RDONLY);
    if (fbfd_ < 0) return false;
    struct stat st;
    if (fstat(fbfd_, &st) < 0 || (size_t)st.st_size < (size_t)width_*height_*4) {
      close(fbfd_); fbfd_ = -1; return false;
    }
    fbsize_ = st.st_size;
    void *m = mmap(NULL, fbsize_, PROT_READ, MAP_SHARED, fbfd_, 0);
    if (m == MAP_FAILED) { close(fbfd_); fbfd_ = -1; return false; }
    fbmap_ = (uint8_t*)m;
    fboff_ = (int)fbsize_ - width_*height_*4;    // Xvfb -fbdir header
    return true;
  }
  void DrawPlaying(Canvas *c) {
    if (child_ > 0 && waitpid(child_, NULL, WNOHANG) == child_) { StopGame(); return; }
    PollPad();
    if (back_ && start_) { StopGame(); return; }        // quit combo
    if (!fbmap_) TryMapFb();
    if (fbmap_ && fboff_ >= 0) {
      const uint8_t *px = fbmap_ + fboff_;
      const int stride = width_ * 4;
      for (int y = 0; y < height_; ++y) {
        const uint8_t *row = px + y * stride;
        for (int x = 0; x < width_; ++x) {
          const uint8_t *p = row + x * 4;
          c->SetPixel(x, y, p[2], p[1], p[0]);          // BGRX -> RGB
        }
      }
    } else {
      c->Fill(0, 0, 8);
      if (fontOK_) rgb_matrix::DrawText(c, font_, 4, height_/2,
                                        rgb_matrix::Color(200,200,60), "LOADING...");
    }
  }

  std::vector<Game> games_;
  int sel_ = 0, top_ = 0, navCd_ = 0;
  bool navFirst_ = true;
  int padfd_ = -1, dpadY_ = 0, stickY_ = 128;
  bool selectEdge_ = false, back_ = false, start_ = false, playing_ = false;
  pid_t child_ = -1;
  int fbfd_ = -1, fboff_ = -1;
  uint8_t *fbmap_ = nullptr; size_t fbsize_ = 0;
  rgb_matrix::Font font_; bool fontTried_ = false, fontOK_ = false;
};

REGISTER_MODE(34, MameMode);
