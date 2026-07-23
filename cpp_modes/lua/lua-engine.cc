// The one definition of the engine's current-instance pointer, which the
// Lua C API callbacks use to find the mode they are drawing into.
#include "lua-engine.h"

LuaScriptMode *LuaScriptMode::s_cur = nullptr;
