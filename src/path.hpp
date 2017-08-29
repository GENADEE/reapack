/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2017  Christian Fillion
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

#ifndef REAPACK_PATH_HPP
#define REAPACK_PATH_HPP

#include <list>

#include "string.hpp"

class UseRootPath;

class Path {
public:
  static const Path DATA;
  static const Path CACHE;
  static const Path CONFIG;
  static const Path REGISTRY;

  static Path prefixRoot(const Path &p) { return s_root + p; }
  static Path prefixRoot(const String &p) { return s_root + p; }
  static const Path &root() { return s_root; }

  Path(const String &path = {});

  void append(const String &parts, bool traversal = true);
  void append(const Path &other);
  void remove(size_t pos, size_t count);
  void removeLast();
  void clear();

  bool empty() const { return m_parts.empty(); }
  size_t size() const { return m_parts.size(); }
  bool absolute() const { return m_absolute; }

  String basename() const;
  Path dirname() const;
  String join(const char sep = 0) const;
  String first() const;
  String last() const;
  bool startsWith(const Path &) const;

  std::list<String>::const_iterator begin() const { return m_parts.begin(); }
  std::list<String>::const_iterator end() const { return m_parts.end(); }

  bool operator==(const Path &) const;
  bool operator!=(const Path &) const;
  bool operator<(const Path &) const;
  Path operator+(const String &) const;
  Path operator+(const Path &) const;
  const Path &operator+=(const String &);
  const Path &operator+=(const Path &);
  String &operator[](size_t);
  const String &operator[](size_t) const;

private:
  static Path s_root;
  friend UseRootPath;

  const String &at(size_t) const;

  std::list<String> m_parts;
  bool m_absolute;
};

class UseRootPath {
public:
  UseRootPath(const Path &);
  ~UseRootPath();

private:
  Path m_backup;
};

class TempPath {
public:
  TempPath(const Path &target);

  const Path &target() const { return m_target; }
  const Path &temp() const { return m_temp; }

private:
  Path m_target;
  Path m_temp;
};

#endif
