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

#ifndef REAPACK_TRANSACTION_HPP
#define REAPACK_TRANSACTION_HPP

#include "download.hpp"
#include "path.hpp"
#include "registry.hpp"

#include <boost/signals2.hpp>
#include <set>

class InstallTask;
class Remote;
class RemoteIndex;
class RemoveTask;
class Task;

class Transaction {
public:
  typedef boost::signals2::signal<void ()> Signal;
  typedef Signal::slot_type Callback;

  typedef std::pair<Version *, const Registry::Entry> PackageEntry;
  typedef std::vector<PackageEntry> PackageEntryList;

  struct Error {
    std::string message;
    std::string title;
  };

  typedef std::vector<const Error> ErrorList;

  Transaction(const Path &root);
  ~Transaction();

  void onFinish(const Callback &callback) { m_onFinish.connect(callback); }
  void onDestroy(const Callback &callback) { m_onDestroy.connect(callback); }

  void synchronize(const Remote &);
  void uninstall(const Remote &);
  void install();
  void cancel();

  bool isCancelled() const { return m_isCancelled; }
  DownloadQueue *downloadQueue() { return &m_queue; }
  size_t taskCount() const { return m_tasks.size(); }
  const PackageEntryList &newPackages() const { return m_new; }
  const PackageEntryList &updates() const { return m_updates; }
  const std::set<Path> &removals() const { return m_removals; }
  const ErrorList &errors() const { return m_errors; }

private:
  friend Task;
  friend InstallTask;
  friend RemoveTask;

  void finish();

  void upgradeAll(Download *);
  bool saveFile(Download *, const Path &);
  void addError(const std::string &msg, const std::string &title);
  Path prefixPath(const Path &) const;
  bool allFilesExists(const std::set<Path> &) const;
  void registerFiles(const std::set<Path> &);
  void addTask(Task *);

  Registry *m_registry;

  Path m_root;
  Path m_dbPath;
  bool m_isCancelled;

  std::vector<RemoteIndex *> m_remoteIndexes;
  DownloadQueue m_queue;
  PackageEntryList m_packages;
  PackageEntryList m_new;
  PackageEntryList m_updates;
  std::set<Path> m_removals;
  ErrorList m_errors;

  std::vector<Task *> m_tasks;
  std::set<Path> m_files;
  bool m_hasConflicts;

  Signal m_onFinish;
  Signal m_onDestroy;
};

#endif
