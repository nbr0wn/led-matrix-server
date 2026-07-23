// Mode registry. Modes register themselves at static-init time (see
// REGISTER_MODE in mode.h); main() asks for the sorted list once at startup.
#include "mode.h"

LuaHost *g_lua_host = nullptr;

namespace {
struct Entry { int order; ModeFactory make; };
// Init-on-first-use: a registrar in another translation unit may run before
// this one's file-scope objects would have been constructed.
std::vector<Entry> &Table() { static std::vector<Entry> t; return t; }
}  // namespace

void RegisterMode(int order, ModeFactory make) { Table().push_back({order, make}); }

std::vector<Mode *> BuildModes() {
  std::vector<Entry> t = Table();
  std::stable_sort(t.begin(), t.end(),
                   [](const Entry &a, const Entry &b) { return a.order < b.order; });
  std::vector<Mode *> modes;
  modes.reserve(t.size());
  for (const Entry &e : t) modes.push_back(e.make());
  return modes;
}
