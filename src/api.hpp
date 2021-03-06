/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2019  Christian Fillion
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

#ifndef REAPACK_API_HPP
#define REAPACK_API_HPP

struct APIFunc {
  const char *cKey;
  void *cImpl;

  const char *reascriptKey;
  void *reascriptImpl;

  const char *definitionKey;
  void *definition;
};

class APIDef {
public:
  APIDef(const APIFunc *);
  APIDef(const APIDef &) = delete;
  ~APIDef();

private:
  void unregister(const char *key, void *ptr);

  const APIFunc *m_func;
};

namespace API {
  // api_misc.cpp
  extern APIFunc BrowsePackages;
  extern APIFunc CompareVersions;
  extern APIFunc ProcessQueue;

  // api_package.cpp
  extern APIFunc AboutInstalledPackage;
  extern APIFunc EnumOwnedFiles;
  extern APIFunc FreeEntry;
  extern APIFunc GetEntryInfo;
  extern APIFunc GetOwner;

  // api_repo.cpp
  extern APIFunc AboutRepository;
  extern APIFunc AddSetRepository;
  extern APIFunc GetRepositoryInfo;
};

#endif
