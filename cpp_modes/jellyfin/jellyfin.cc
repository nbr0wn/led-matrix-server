// JellyfinMode -- registered as mode 34.
#include "common/mode.h"
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

// --- 35: Jellyfin video player --------------------------------------------
// Searches a Jellyfin server for a named title and plays the first match on
// the matrix. Set the title over telnet, e.g. "35 The Matrix". A background
// thread searches via the REST API (curl), then pipes the stream through
// ffmpeg (decode + scale to the panel, looping) and blits the raw frames.
class JellyfinMode : public Mode {
public:
  const char *name() const override { return "Jellyfin"; }
  bool TakesArg() const override { return true; }
  const char *ArgHint() const override { return "\"title\" [start [end]]"; }
  const char *ArgUsage() const override {
    return "Title, then optional start and end offsets (secs, m:ss or h:mm:ss). "
           "Quote a title containing numbers. An end offset loops that segment; "
           "without one the movie replays from the start offset. "
           "e.g. Alien 90  |  \"Blade Runner 2049\" 1:30 2:00";
  }
  void SetArg(const std::string &arg) override {
    std::string clean;
    for (char c : arg) if (c >= 32 && c < 127) clean += c;   // printable ASCII
    Trim(clean);
    // The title may be quoted ("..." or '...') so it can contain numbers/spaces;
    // up to two optional trailing times are the start offset and the end of the
    // segment to loop. Each is plain seconds, m:ss or h:mm:ss.
    int start = 0, end = 0;
    std::string title;
    if (!clean.empty() && (clean[0] == '"' || clean[0] == '\'')) {
      const char q = clean[0];
      const size_t close = clean.find(q, 1);
      if (close != std::string::npos) {
        title = clean.substr(1, close - 1);
        std::string rest = clean.substr(close + 1);
        Trim(rest);
        ParseTimeTail(rest, start, end);
      } else {
        title = clean.substr(1);        // unterminated quote: rest is the title
      }
    } else {
      title = clean;                    // unquoted: trailing numbers are the times
      std::string tail;                 // rebuilt left-to-right as tokens come off
      for (int i = 0; i < 2; ++i) {
        const size_t sp = title.find_last_of(' ');
        if (sp == std::string::npos || sp + 1 >= title.size()) break;
        const std::string last = title.substr(sp + 1);
        int v;
        if (!ParseTime(last, v)) break;
        tail = tail.empty() ? last : last + " " + tail;
        title = title.substr(0, sp);
        Trim(title);
      }
      ParseTimeTail(tail, start, end);
    }
    Trim(title);
    if (start < 0) start = 0;
    if (start > 86400) start = 86400;
    if (end > 86400) end = 86400;
    if (end <= start) end = 0;          // 0 = play to the end of the movie
    std::lock_guard<std::mutex> lk(mtx_);
    // The web page saves the textbox on change *and* submits it with the button,
    // so the same value can arrive twice; restarting twice would reload the
    // stream for nothing. Only a real change (or a retry after a failure) counts.
    if (title == query_ && start == startSecs_ && end == endSecs_ && !errored_) return;
    query_ = title;
    title_ = title;                     // display title (updated to the matched name later)
    startSecs_ = start;
    endSecs_ = end;
    status_ = title.empty() ? "loading..." : ("searching: " + title);
    errored_ = false;
    hasFrame_ = false;
    ++reqGen_;
  }
  // Leaving the mode: let the worker drop out of its read loop right away so it
  // can tell Jellyfin to stop transcoding, instead of the server running on for
  // the two seconds the lastDraw_ watchdog would take. Render thread -- must not
  // block, so the actual DELETE happens on the worker.
  void Deactivate() override { active_.store(false); }
  void Activate(int w, int h) override {
    Mode::Activate(w, h);
    active_.store(true);
    lastDraw_.store((long)time(NULL));
    if (!fontTried_) {
      fontTried_ = true;
      const char *cands[] = { "fonts/6x10.bdf", "fonts/5x7.bdf",
                              "/root/led-matrix-server/fonts/6x10.bdf" };
      for (const char *p : cands) if (font_.LoadFont(p)) { fontOK_ = true; break; }
    }
    if (!started_) {
      started_ = true;
      std::thread(&JellyfinMode::Worker, this).detach();
    } else {
      std::lock_guard<std::mutex> lk(mtx_);   // re-selected: resume playback
      errored_ = false;
      hasFrame_ = false;                       // show the title/skip loading screen again
      ++reqGen_;
    }
  }
  void Draw(Canvas *c, float t, float dt) override {
    lastDraw_.store((long)time(NULL));
    bool has, err; std::string st, title; int start, end;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      has = hasFrame_;
      if (has) disp_ = frame_;
      st = status_; err = errored_; title = title_;
      start = startSecs_; end = endSecs_;
    }
    if (has && (int)disp_.size() >= width_*height_*3) {
      size_t i = 0;
      for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x, i += 3)
          c->SetPixel(x, y, disp_[i], disp_[i+1], disp_[i+2]);
      return;
    }
    c->Fill(0, 0, 8);
    if (!fontOK_) return;
    if (err) {                                   // a real failure: show what went wrong
      DrawCentered(c, st, height_/2, rgb_matrix::Color(255, 120, 120));
      return;
    }
    // ffmpeg still loading: just the movie title and the start time (or the
    // start-end range when a segment is being looped).
    char sk[48];
    if (end > start) snprintf(sk, sizeof sk, "loop: %s-%s", Hms(start).c_str(), Hms(end).c_str());
    else             snprintf(sk, sizeof sk, "time: %s", Hms(start).c_str());
    DrawCentered(c, title.empty() ? "Loading..." : title, height_/2 - 2,
                 rgb_matrix::Color(200, 220, 255));
    DrawCentered(c, sk, height_/2 + font_.height(), rgb_matrix::Color(120, 140, 180));
  }
private:
  // Draw one line of text horizontally centered on the panel (fixed-width font),
  // truncating with ".." if it would overflow. baselineY is the text baseline.
  void DrawCentered(Canvas *c, std::string s, int baselineY, const rgb_matrix::Color &col) {
    int cw = font_.CharacterWidth('0'); if (cw <= 0) cw = 6;
    const int maxch = (width_ - 2) / cw;
    if (maxch > 2 && (int)s.size() > maxch) s = s.substr(0, maxch - 2) + "..";
    int x = (width_ - (int)s.size() * cw) / 2;
    if (x < 1) x = 1;
    rgb_matrix::DrawText(c, font_, x, baselineY, col, s.c_str());
  }
  // Server + API key come from the environment (JELLYFIN_HOST like "host:8096",
  // JELLYFIN_API_KEY), so no credentials live in the source. The app loads them
  // from the untracked led-matrix-server.conf into the environment at startup;
  // unset means the mode shows a config hint.
  static std::string Host() { const char *h = getenv("JELLYFIN_HOST");    return h ? h : ""; }
  static std::string Key()  { const char *k = getenv("JELLYFIN_API_KEY"); return k ? k : ""; }
  static std::string UrlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
      const bool an = (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z');
      if (an) o += (char)c;
      else { o += '%'; o += hex[c>>4]; o += hex[c&15]; }
    }
    return o;
  }
  static std::string Hms(int s) {
    char b[16]; snprintf(b, sizeof b, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    return b;
  }
  // "90", "1:30" (m:ss) or "1:02:03" (h:mm:ss) -> seconds. False if `s` is not
  // a time, which is how a trailing word is told apart from a trailing offset.
  static bool ParseTime(const std::string &s, int &out) {
    if (s.empty() || s.size() > 9) return false;
    int part[3] = {0, 0, 0}, n = 0, cur = 0;
    bool digit = false;
    for (char c : s) {
      if (c >= '0' && c <= '9') {
        cur = cur * 10 + (c - '0');
        if (cur > 86400) return false;
        digit = true;
      } else if (c == ':') {
        if (!digit || n == 2) return false;
        part[n++] = cur; cur = 0; digit = false;
      } else {
        return false;
      }
    }
    if (!digit) return false;
    part[n] = cur;
    out = 0;
    for (int i = 0; i <= n; ++i) out = out * 60 + part[i];
    return true;
  }
  // Parse "" | "<start>" | "<start> <end>" from the tail of an argument.
  static void ParseTimeTail(const std::string &s, int &start, int &end) {
    int v[2] = {0, 0}, got = 0;
    for (size_t i = 0; i < s.size() && got < 2; ) {
      while (i < s.size() && s[i] == ' ') ++i;
      size_t j = i;
      while (j < s.size() && s[j] != ' ') ++j;
      if (j == i || !ParseTime(s.substr(i, j - i), v[got])) break;
      ++got; i = j;
    }
    if (got >= 1) start = v[0];
    if (got >= 2) end = v[1];
  }
  static void Trim(std::string &s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  }
  bool Http(const std::string &url, std::string &out) {
    const std::string cmd = "curl -s -m 8 '" + url + "'";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char b[4096]; size_t k; out.clear();
    while ((k = fread(b, 1, sizeof b, p)) > 0) out.append(b, k);
    pclose(p);
    return !out.empty();
  }
  static std::string Lower(std::string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
  }
  // Normalize for exact-title comparison: trim surrounding whitespace + lowercase.
  static std::string Norm(std::string s) { Trim(s); return Lower(s); }
  // Extract a JSON string field ("key":"value") from a flat-ish object slice.
  static std::string JsonStr(const std::string &o, const char *key) {
    const std::string k = std::string("\"") + key + "\":\"";
    const size_t p = o.find(k);
    if (p == std::string::npos) return "";
    const size_t s = p + k.size(), e = o.find('"', s);
    return e == std::string::npos ? std::string() : o.substr(s, e - s);
  }
  // Walk the first JSON array in `body` (named `arrKey`), respecting nested
  // braces and quoted strings, and pick the best item: an exact case-insensitive
  // Name match to `q` if present, else the first (top-ranked) element with an id.
  std::string PickBest(const std::string &body, const char *arrKey,
                       const std::string &q, std::string &name) {
    const size_t k = body.find(std::string("\"") + arrKey + "\"");
    if (k == std::string::npos) return "";
    size_t i = body.find('[', k);
    if (i == std::string::npos) return "";
    const std::string ql = Norm(q);
    std::string firstId, firstName;
    int depth = 0; bool inStr = false, esc = false; size_t objStart = std::string::npos;
    for (++i; i < body.size(); ++i) {
      const char c = body[i];
      if (inStr) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') inStr = false; continue; }
      if (c == '"') { inStr = true; continue; }
      if (c == '{') { if (depth == 0) objStart = i; ++depth; }
      else if (c == '}') {
        if (--depth == 0 && objStart != std::string::npos) {
          const std::string obj = body.substr(objStart, i - objStart + 1);
          std::string id = JsonStr(obj, "ItemId");
          if (id.empty()) id = JsonStr(obj, "Id");
          const std::string nm = JsonStr(obj, "Name");
          if (!id.empty()) {
            if (firstId.empty()) { firstId = id; firstName = nm; }
            if (!ql.empty() && Norm(nm) == ql) { name = nm; return id; }   // exact title wins
          }
          objStart = std::string::npos;
        }
      } else if (c == ']' && depth == 0) break;    // end of the array
    }
    if (!firstId.empty()) { name = firstName; return firstId; }
    return "";
  }
  std::string Search(const std::string &q, std::string &name) {
    std::string body;
    // 1) Relevance-ranked search: /Search/Hints returns best matches first, so
    //    "Raiders of the Lost Ark" ranks above fuzzy hits like "The Raid".
    std::string u1 = "http://" + Host() + "/Search/Hints?api_key=" + Key() +
                     "&IncludeItemTypes=Movie&Limit=12";
    if (!q.empty()) u1 += "&searchTerm=" + UrlEncode(q);
    if (Http(u1, body)) {
      const std::string id = PickBest(body, "SearchHints", q, name);
      if (!id.empty()) return id;
    }
    // 2) Fallback: item query. No Limit=1/SortName truncation, and PickBest still
    //    prefers an exact title match among the results.
    std::string u2 = "http://" + Host() + "/Items?api_key=" + Key() +
      "&Recursive=true&IncludeItemTypes=Movie&Limit=25";
    if (!q.empty()) u2 += "&searchTerm=" + UrlEncode(q);
    if (Http(u2, body)) return PickBest(body, "Items", q, name);
    return "";
  }
  // A stable device id for this process, so one DELETE can stop every transcode
  // job the server is running for us -- including one we lost track of.
  const std::string &DeviceId() {
    if (deviceId_.empty()) {
      char b[32]; snprintf(b, sizeof b, "matrix-%d", (int)getpid());
      deviceId_ = b;
    }
    return deviceId_;
  }
  // Tell Jellyfin to stop transcoding for us, and to delete the job's output.
  // While a job for an item is alive the server replays *its* .ts from the top
  // for the next request on that same item, which is why re-asking for the same
  // movie at a new offset played from the beginning while switching movies
  // worked. The endpoint matches on playSessionId when one is given and on
  // deviceId otherwise, so send both as separate requests: the session kill
  // targets the job we started, the device sweep catches anything stale (the
  // deviceId the server records comes from the auth context, so it may not be
  // the one we asked for). Blocking curl -- worker thread only.
  void StopEncodings(const std::string &sid) {
    const std::string base = "curl -s -m 3 -o /dev/null -X DELETE 'http://" + Host() +
                             "/Videos/ActiveEncodings?api_key=" + Key();
    if (!sid.empty())
      if (FILE *p = popen((base + "&playSessionId=" + sid + "'").c_str(), "r")) pclose(p);
    if (FILE *p = popen((base + "&deviceId=" + DeviceId() + "'").c_str(), "r")) pclose(p);
  }
  // Each playback attempt gets its own play session, so the server treats it as
  // a new request rather than a resumption of the one already in flight.
  std::string NewSessionId() {
    char b[48]; snprintf(b, sizeof b, "%s-%d", DeviceId().c_str(), ++playSeq_);
    return b;
  }
  enum PlayResult { kInterrupted,   // new request, paused, or shutting down
                    kEnded,         // stream ran to its end -- caller should loop
                    kFailed };      // never produced a frame
  // `discard` > 0 marks a loop restart: play that many buffered frames while
  // throwing away the same number from the front of the new stream. startFrame
  // stays at the segment's start on every pass, so the stream reproduces exactly
  // what the buffer holds and the two line up frame for frame.
  PlayResult PlayStream(const std::string &id, int gen, long startFrame, int end, long discard) {
    // Ask Jellyfin to TRANSCODE server-side down to ~256x144 h264 and to start at
    // the skip position (startTimeTicks, in 100ns units), so the Pi only has to
    // decode a tiny stream instead of the (possibly 4K) original.
    const std::string sid = NewSessionId();
    if (discard > 0) {
      lastSid_ = sid;    // the previous job was killed as the last pass ended
    } else {
      // Drop the previous job before asking for a new one, or the server just
      // hands the old one back. The kill is asynchronous, so give it a moment to
      // land before re-requesting -- otherwise we race the job we just killed.
      StopEncodings(lastSid_);
      lastSid_ = sid;
      usleep(500000);
    }
    // EnableAutoStreamCopy=false forces a real transcode: on a stream copy the
    // server is free to hand back the whole file and ignore startTimeTicks.
    char url[768];
    snprintf(url, sizeof url,
             "http://%s/Videos/%s/stream.ts?api_key=%s&static=false"
             "&videoCodec=h264&audioCodec=aac&maxWidth=%d&maxHeight=%d"
             "&videoBitRate=1000000&mediaSourceId=%s&startTimeTicks=%lld"
             "&EnableAutoStreamCopy=false&PlaySessionId=%s&DeviceId=%s&_=%d",
             Host().c_str(), id.c_str(), Key().c_str(), g_video_w, g_video_h,
             id.c_str(), (long long)startFrame * 10000000LL / kFps, sid.c_str(),
             DeviceId().c_str(), playSeq_);
    // Fill the whole panel: scale up preserving aspect until it covers the
    // panel (increase), then center-crop the overflow to exactly WxH. This
    // avoids letterbox/pillarbox bars when the source aspect (e.g. 16:9) differs
    // from the panel's (256x128 = 2:1); a little of the long edge is cropped.
    char vf[192];
    snprintf(vf, sizeof vf,
             "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d",
             width_, height_, width_, height_);
    // No -re: the loop restart has to burn through the frames the buffer already
    // covers *faster* than realtime to catch up before the buffer runs out, and
    // -re would hold it to 1x. Playback is paced on our own clock instead, and
    // the pipe back-pressures ffmpeg whenever we are the slower end.
    // -t stops at the end offset so a segment can be looped.
    const long endFrame = (long)end * kFps;
    char dur[32] = "";
    if (end > 0 && endFrame > startFrame)
      snprintf(dur, sizeof dur, "-t %.3f ", (double)(endFrame - startFrame) / kFps);
    const std::string cmd =
      "ffmpeg -loglevel quiet -nostdin -i '" + std::string(url) +
      "' -an " + dur + "-f rawvideo -pix_fmt rgb24 -vf '" + std::string(vf) +
      "' -r " + std::to_string(kFps) + " - 2>/dev/null";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) { std::lock_guard<std::mutex> lk(mtx_); status_ = "ffmpeg error"; errored_ = true; return kFailed; }
    { std::lock_guard<std::mutex> lk(mtx_); status_ = "playing"; errored_ = false; }
    const int fd = fileno(p);
    // A default 64KB pipe holds less than one 96KB frame, so ffmpeg could only
    // ever be one partial frame ahead -- the loop restart could never catch up
    // during the cover. Give it room to run ahead by tens of frames instead.
    if (fcntl(fd, F_SETPIPE_SZ, 4 << 20) < 0) fcntl(fd, F_SETPIPE_SZ, 1 << 20);
    const size_t need = (size_t)width_ * height_ * 3;
    std::vector<uint8_t> buf(need);
    // We pace playback ourselves (see the -re note above), on one clock that
    // runs across the cover and the stream so the join keeps cadence.
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    // Hide the transcoder's startup behind the buffered head. Runs after popen,
    // so ffmpeg is already warming up while the buffer plays.
    if (discard > 0 && !CoverAndDiscard(gen, fd, discard, &next)) {
      pclose(p); StopEncodings(sid); return kInterrupted;
    }
    PlayResult res = kEnded;
    long frames = 0;
    // On the cold pass, keep the first kCacheSecs of decoded frames. That is all
    // the loop needs: a transcode job is a one-way pipe that cannot be rewound,
    // so the restart is unavoidable -- but a few seconds of RAM covers it.
    const long capFrames = (long)kCacheSecs * kFps;
    const long wantFrames = (end > 0 && endFrame > startFrame) ? endFrame - startFrame : 0;
    const bool caching = discard == 0;
    if (caching) {
      cache_.clear();
      cache_.reserve((size_t)capFrames * need);
      cacheWholeSegment_ = false;
    }
    while (g_running.load()) {
      { std::lock_guard<std::mutex> lk(mtx_); if (reqGen_ != gen) { res = kInterrupted; break; } }
      if (!active_.load()) { res = kInterrupted; break; }                          // switched away
      if ((long)time(NULL) - lastDraw_.load() > 2) { res = kInterrupted; break; }  // paused: mode not on screen
      size_t off = 0;
      long got = 0;
      if (!Drain(fd, buf, off, got, 1)) break;   // EOF or error -> kEnded, caller restarts us
      // Outside the lock: cache_ belongs to this thread, and Draw() shouldn't
      // wait on a 100KB copy.
      if (caching && (long)(cache_.size() / need) < capFrames)
        cache_.insert(cache_.end(), buf.begin(), buf.end());
      WaitFrame(&next);                          // hold this frame's slot
      std::lock_guard<std::mutex> lk(mtx_);
      if (reqGen_ != gen) { res = kInterrupted; break; }
      frame_.swap(buf); hasFrame_ = true; buf.resize(need); ++frames;
    }
    if (caching) {
      // A pass cut short may have captured a partial head -- fine to cover with,
      // but only trust it if we reached the end. A segment that fits entirely in
      // the cache needs no restart at all.
      if (res != kEnded) { cache_.clear(); cache_.shrink_to_fit(); }
      else cacheWholeSegment_ = wantFrames > 0 && wantFrames <= capFrames &&
                                frames >= wantFrames - wantFrames / 10;
    }
    if (!g_running.load()) res = kInterrupted;
    pclose(p);
    StopEncodings(sid);
    lastSid_.clear();                                  // already killed
    if (res == kEnded && frames == 0) res = kFailed;   // a start offset past the end, say
    std::lock_guard<std::mutex> lk(mtx_);
    if (res == kFailed && reqGen_ == gen) {
      status_ = "playback failed"; errored_ = true; hasFrame_ = false;
    }
    return res;
  }
  // Advance `next` by one frame period and sleep until it. Re-bases when we have
  // fallen a whole second behind, so a hitch doesn't turn into a sprint.
  static void WaitFrame(struct timespec *next) {
    next->tv_nsec += 1000000000L / kFps;
    if (next->tv_nsec >= 1000000000L) { next->tv_nsec -= 1000000000L; ++next->tv_sec; }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > next->tv_sec) *next = now;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next, NULL);
  }
  // Pull whole frames out of `fd` into scratch, carrying a partial frame across
  // calls in `off`. Stops when `want` frames have been taken, when nothing more
  // has arrived yet (non-blocking fd), or at EOF -- which the false return
  // distinguishes from merely having caught up.
  static bool Drain(int fd, std::vector<uint8_t> &scratch, size_t &off,
                    long &taken, long want) {
    const size_t need = scratch.size();
    while (taken < want) {
      const ssize_t r = read(fd, scratch.data() + off, need - off);
      if (r > 0) { off += (size_t)r; if (off == need) { off = 0; ++taken; } continue; }
      if (r < 0 && errno == EINTR) continue;
      return r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);   // else EOF/error
    }
    return true;
  }
  // Replay the whole buffered segment from RAM, forever. Only used when the
  // segment fit entirely in the cache, so there is no stream to fall back to.
  bool PlayCache(int gen) {
    const size_t need = (size_t)width_ * height_ * 3;
    const size_t n = cache_.size() / need;
    if (n == 0) return true;
    { std::lock_guard<std::mutex> lk(mtx_); status_ = "looping"; errored_ = false; }
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (size_t i = 0; ; i = (i + 1) % n) {
      {
        std::lock_guard<std::mutex> lk(mtx_);
        if (reqGen_ != gen) return false;
        if ((long)time(NULL) - lastDraw_.load() > 2) return false;   // paused: not on screen
        frame_.assign(cache_.begin() + i * need, cache_.begin() + (i + 1) * need);
        hasFrame_ = true;
      }
      if (!g_running.load() || !active_.load()) return false;
      WaitFrame(&next);
    }
    return true;
  }
  // Loop restart. Play the buffered head on the frame clock while draining, in
  // the gaps between frames, the frames of the new stream that duplicate it --
  // so when the buffer runs out the stream sits exactly on the next frame.
  //
  // The restart asks for the *same* offset the buffer was captured at, so frame
  // k of this stream is frame k of the buffer and the join is exact by
  // construction. Asking the server to seek to the join point instead cannot be
  // frame-perfect: it snaps to a keyframe and lands a few frames off, which is
  // the jump that was showing up here.
  bool CoverAndDiscard(int gen, int fd, long n, struct timespec *next) {
    const size_t need = (size_t)width_ * height_ * 3;
    const int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);       // drain without stalling the clock
    std::vector<uint8_t> scratch(need);
    size_t off = 0;
    long dropped = 0;
    bool live = true;
    for (long i = 0; i < n; ++i) {
      {
        std::lock_guard<std::mutex> lk(mtx_);
        if (reqGen_ != gen) { fcntl(fd, F_SETFL, fl); return false; }
        if ((long)time(NULL) - lastDraw_.load() > 2) { fcntl(fd, F_SETFL, fl); return false; }
        frame_.assign(cache_.begin() + i * need, cache_.begin() + (i + 1) * need);
        hasFrame_ = true;
      }
      if (!g_running.load() || !active_.load()) { fcntl(fd, F_SETFL, fl); return false; }
      if (live) live = Drain(fd, scratch, off, dropped, n);
      WaitFrame(next);
    }
    fcntl(fd, F_SETFL, fl);                    // back to blocking for playback
    // If the server could not stay ahead of realtime, finish the discard here.
    // That shows as a brief hold on the last buffered frame -- still correct,
    // just not hidden.
    if (live && dropped < n) Drain(fd, scratch, off, dropped, n);
    return true;
  }
  void Worker() {
    while (g_running.load()) {
      int gen, start, end; std::string q;
      { std::lock_guard<std::mutex> lk(mtx_);
        gen = reqGen_; q = query_; start = startSecs_; end = endSecs_; }
      if (gen == servedGen_) { usleep(100000); continue; }
      servedGen_ = gen;
      if (Host().empty() || Key().empty()) {        // no credentials configured
        std::lock_guard<std::mutex> lk(mtx_);
        if (reqGen_ == gen) { status_ = "set JELLYFIN_HOST + JELLYFIN_API_KEY"; errored_ = true; }
        continue;
      }
      std::string name;
      const std::string id = Search(q, name);
      { std::lock_guard<std::mutex> lk(mtx_); if (reqGen_ != gen) continue; }
      if (id.empty()) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (reqGen_ == gen) { status_ = q.empty() ? "server unreachable" : ("not found: " + q); errored_ = true; }
        continue;
      }
      { std::lock_guard<std::mutex> lk(mtx_); if (reqGen_ == gen && !name.empty()) title_ = name; }
      // Play, then re-open at the start offset when the stream ends: ffmpeg's
      // -stream_loop can't rewind an HTTP stream, so without this the panel just
      // froze on the last frame at the end of the movie/segment. The first pass
      // banks the opening kCacheSecs; every later pass replays those from RAM
      // while the transcoder restarts behind them, and resumes the stream just
      // past them -- so the wrap is seamless without a second transcode job.
      const size_t need = (size_t)width_ * height_ * 3;
      const long startFrame = (long)start * kFps;
      long discard = 0;
      for (;;) {
        const long t0 = (long)time(NULL);
        if (PlayStream(id, gen, startFrame, end, discard) != kEnded) break;
        { std::lock_guard<std::mutex> lk(mtx_); if (reqGen_ != gen) break; }
        if (!g_running.load() || !active_.load()) break;
        if (cacheWholeSegment_) { PlayCache(gen); break; }   // no stream needed at all
        // Skip exactly as many frames as the buffer will have played out.
        discard = (long)(cache_.size() / need);
        if (discard == 0 && (long)time(NULL) - t0 < 2) usleep(500000);   // don't spin on instant EOF
      }
      cache_.clear();
      cache_.shrink_to_fit();
      cacheWholeSegment_ = false;
    }
  }

  std::mutex mtx_;
  std::string query_;           // requested title (empty = first available)
  std::string title_;           // title shown on the loading screen (matched name)
  std::string status_ = "loading...";
  std::vector<uint8_t> frame_, disp_;
  bool hasFrame_ = false, started_ = false, fontTried_ = false, fontOK_ = false;
  bool errored_ = false;        // true when status_ is a failure worth showing
  int reqGen_ = 1, servedGen_ = 0;
  int startSecs_ = 0, endSecs_ = 0;   // endSecs_ 0 = play to the end of the movie
  int playSeq_ = 0;                   // bumped per ffmpeg spawn, for the session id
  std::string deviceId_, lastSid_;    // worker thread only
  std::vector<uint8_t> cache_;        // opening frames, for a gapless loop; worker only
  bool cacheWholeSegment_ = false;    // the cache is the entire segment: never restream
  static const int kFps = 25;         // matches ffmpeg's -r
  static const int kCacheSecs = 8;    // ~20MB at 256x128x3; covers a transcode restart
  std::atomic<bool> active_{false};   // false once the panel has moved on
  std::atomic<long> lastDraw_{0};
  rgb_matrix::Font font_;
};

REGISTER_MODE(33, JellyfinMode);
