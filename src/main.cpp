/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2016  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "errors.hpp"
#include "menu.hpp"
#include "reapack.hpp"

#include <vector>

#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

using namespace std;

static ReaPack *reapack = nullptr;

#define REQUIRED_API(name) {(void **)&name, #name, true}
#define OPTIONAL_API(name) {(void **)&name, #name, false}

static bool loadAPI(void *(*getFunc)(const char *))
{
  struct ApiFunc { void **ptr; const char *name; bool required; };

  const vector<ApiFunc> funcs = {

    REQUIRED_API(AddExtensionsMainMenu),
    REQUIRED_API(file_exists),
    REQUIRED_API(GetAppVersion),
    REQUIRED_API(GetMainHwnd),
    REQUIRED_API(GetResourcePath),
    REQUIRED_API(GetUserFileNameForRead),    // v3.21
    REQUIRED_API(NamedCommandLookup),        // v3.1415
    REQUIRED_API(plugin_register),
    REQUIRED_API(RecursiveCreateDirectory),  // v4.60
    REQUIRED_API(ReverseNamedCommandLookup), // v4.7
    REQUIRED_API(ShowMessageBox),

    OPTIONAL_API(AddRemoveReaScript),        // v5.12
  };

  bool ok = true;

  for(const ApiFunc &func : funcs) {
    *func.ptr = getFunc(func.name);
    ok = ok && (*func.ptr || !func.required);
  }

  return ok;
}

#undef REQUIRED_API
#undef OPTIONAL_API

static bool commandHook(const int id, const int flag)
{
  return reapack->execActions(id, flag);
}

static void menuHook(const char *name, HMENU handle, int f)
{
  if(strcmp(name, "Main extensions") || f != 0)
    return;

  Menu menu = Menu(handle).addMenu(AUTO_STR("ReaPack"));

  menu.addAction(AUTO_STR("Synchronize packages"),
    NamedCommandLookup("_REAPACK_SYNC"));

  menu.addAction(AUTO_STR("Import remote repository..."),
    NamedCommandLookup("_REAPACK_IMPORT"));

  menu.addAction(AUTO_STR("Manage remotes..."),
    NamedCommandLookup("_REAPACK_MANAGE"));

  menu.addSeparator();

  menu.addAction(AUTO_STR("About ReaPack v0.1"), 0);
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  if(!rec) {
    plugin_register("-hookcommand", (void *)commandHook);
    plugin_register("-hookcustommenu", (void *)menuHook);

    delete reapack;

    return 0;
  }

  if(rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
    return 0;

  if(!loadAPI(rec->GetFunc))
    return 0;

  reapack = new ReaPack(instance);

  reapack->setupAction("REAPACK_SYNC", "ReaPack: Synchronize packages",
    &reapack->syncAction, bind(&ReaPack::synchronizeAll, reapack));

  reapack->setupAction("REAPACK_IMPORT", "ReaPack: Import remote repository...",
    &reapack->importAction, bind(&ReaPack::importRemote, reapack));

  reapack->setupAction("REAPACK_MANAGE", "ReaPack: Manage remotes...",
    &reapack->configAction, bind(&ReaPack::manageRemotes, reapack));

  plugin_register("hookcommand", (void *)commandHook);
  plugin_register("hookcustommenu", (void *)menuHook);

  AddExtensionsMainMenu();

  return 1;
}

#ifdef __APPLE__
#include "resource.hpp"

#include <swell/swell-dlggen.h>
#include "resource.rc_mac_dlg"

#include <swell/swell-menugen.h>
#include "resource.rc_mac_menu"
#endif
