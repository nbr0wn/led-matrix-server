// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// Fetched Lua modules: display modes downloaded at runtime from one or more
// remote catalogs, rather than compiled in. They run on the same sandboxed
// engine as the built-in Lua Script mode (cpp_modes/lua/lua-engine.h) and are
// appended after the built-in modes in the mode list.
//
// A catalog is a JSON manifest at a URL:
//
//   { "name": "Nick's modules",
//     "modules": [
//       { "id": "warp", "name": "Warp Tunnel", "desc": "Starfield in a tunnel",
//         "script": "warp.lua", "shot": "warp.png" } ] }
//
// `script` and `shot` are resolved relative to the manifest URL. The app only
// ever downloads the manifest and the .lua scripts -- screenshots are loaded
// straight from the catalog host by the browser showing the web UI, so no
// image data is proxied through or stored on the Pi.
//
// Threading: everything here except BuildModes()/TakeDirty() is called from the
// web server thread (it does network I/O and can block for seconds). Installing
// or removing sets a dirty flag; the render thread notices it between frames
// and rebuilds the mode list, so the vector is never mutated under a Draw().
#ifndef LED_MATRIX_SERVER_MODULES_H
#define LED_MATRIX_SERVER_MODULES_H

#include "../common/mode.h"

namespace modules {

struct ModuleInfo {
  std::string id, name, desc;
  std::string script_url, shot_url;
  bool installed = false;
};

struct CatalogInfo {
  std::string url, name, error;      // `error` non-empty => last fetch failed
  std::vector<ModuleInfo> modules;
};

// --- called from the web server thread ------------------------------------

// Catalogs, read from the on-disk cache (no network). Each module is marked
// with whether it is currently installed.
std::vector<CatalogInfo> Catalogs();

// Add a catalog URL and fetch its manifest. Returns "" on success, else why not.
std::string AddCatalog(const std::string &url);

// Forget a catalog. Modules already installed from it stay installed.
void RemoveCatalog(const std::string &url);

// Re-download every catalog manifest. Returns "" or a combined error string.
std::string RefreshCatalogs();

// Fetch the fixed GitHub catalog at startup (background; no-op if already cached).
void EnsureCatalog();

// Make the installed set exactly `keys` (each "<catalog url>\n<module id>",
// as produced by Key()). Downloads what is newly checked, deletes what is
// unchecked, and marks the mode list dirty. Returns "" or an error summary.
std::string ApplySelection(const std::vector<std::string> &keys);

// The key identifying one module within one catalog.
std::string Key(const std::string &catalog_url, const std::string &id);

// --- called from the render thread ----------------------------------------

// True (once) if the installed set changed and the mode list needs rebuilding.
bool TakeDirty();

// Names of modules installed since this was last called, so the app can put
// them in the auto-cycle set -- you asked for them, so they should cycle.
std::vector<std::string> TakeNewlyAdded();

// A freshly built mode per installed module, in installed order.
std::vector<Mode *> BuildModes();

// How many modules are installed (for the UI, cheap).
size_t InstalledCount();

}  // namespace modules

#endif  // LED_MATRIX_SERVER_MODULES_H
