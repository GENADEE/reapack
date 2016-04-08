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

#include "transaction.hpp"

#include "encoding.hpp"
#include "errors.hpp"
#include "filesystem.hpp"
#include "index.hpp"
#include "remote.hpp"
#include "task.hpp"

#include <boost/algorithm/string.hpp>

#include <reaper_plugin_functions.h>

using namespace std;

Transaction::Transaction()
  : m_isCancelled(false)
{
  m_registry = new Registry(Path::prefixRoot(Path::REGISTRY));

  // don't keep pre-install pushes (for conflict checks); released in runTasks
  m_registry->savepoint();

  m_downloadQueue.onAbort([=] {
    m_isCancelled = true;

    for(Task *task : m_tasks)
      task->rollback();

    if(m_downloadQueue.idle())
      finish();
  });

  m_downloadQueue.onDone([=] {
    if(m_installQueue.empty())
      finish();
    else
      runTasks();
  });
}

Transaction::~Transaction()
{
  for(Task *task : m_tasks)
    delete task;

  delete m_registry;
}

void Transaction::synchronize(const Remote &remote, const bool autoInstall)
{
  // show the report dialog or "nothing to do" even if no task are ran
  m_receipt.setEnabled(true);

  fetchIndex(remote, [=] {
    IndexPtr ri;

    try {
      ri = Index::load(remote.name());
    }
    catch(const reapack_error &e) {
      // index file is invalid (load error)
      addError(e.what(), remote.name());
      return;
    }

    for(const Package *pkg : ri->packages())
      synchronize(pkg, autoInstall);
  });
}

void Transaction::synchronize(const Package *pkg, const bool autoInstall)
{
  const auto &regEntry = m_registry->getEntry(pkg);

  if(!regEntry.id && !autoInstall)
    return;

  const Version *latest = pkg->lastVersion();

  if(regEntry.version == *latest) {
    if(allFilesExists(latest->files()))
      return; // latest version is really installed, nothing to do here!
  }

  install(latest, regEntry);
}

void Transaction::fetchIndex(const Remote &remote, const IndexCallback &cb)
{
  // add the callback to the list, and start the download if it's the first one
  const std::string &name = remote.name();
  m_remotes.insert({name, cb});

  if(m_remotes.count(name) > 1)
    return;

  Download *dl = Index::fetch(remote, true);

  if(!dl) {
    // the index was last downloaded less than a few seconds ago
    cb();
    return;
  }

  m_downloadQueue.push(dl);
  dl->onFinish(bind(&Transaction::saveIndex, this, dl, name));
}

void Transaction::saveIndex(Download *dl, const string &name)
{
  if(!saveFile(dl, Index::pathFor(name)))
    return;

  const auto end = m_remotes.upper_bound(name);

  for(auto it = m_remotes.lower_bound(name); it != end; it++)
    it->second();
}

void Transaction::install(const Version *ver)
{
  install(ver, m_registry->getEntry(ver->package()));
}

void Transaction::install(const Version *ver,
  const Registry::Entry &regEntry)
{
  m_receipt.setEnabled(true);

  InstallTicket::Type type;

  if(regEntry.id && regEntry.version < *ver)
    type = InstallTicket::Upgrade;
  else
    type = InstallTicket::Install;

  // prevent file conflicts (don't worry, the registry push is reverted in runTasks)
  try {
    vector<Path> conflicts;
    m_registry->push(ver, &conflicts);

    if(!conflicts.empty()) {
      for(const Path &path : conflicts) {
        addError("Conflict: " + path.join() +
          " is already owned by another package",
          ver->fullName());
      }

      return;
    }
  }
  catch(const reapack_error &e) {
    // handle database error from Registry::push
    addError(e.what(), ver->fullName());
    return;
  }

  // all green! (pronounce with a japanese accent)
  IndexPtr ri = ver->package()->category()->index()->shared_from_this();
  if(!m_indexes.count(ri))
    m_indexes.insert(ri);

  m_installQueue.push({type, ver, regEntry});
}

void Transaction::installTicket(const InstallTicket &ticket)
{
  const Version *ver = ticket.version;
  const set<Path> &currentFiles = m_registry->getFiles(ticket.regEntry);

  InstallTask *task = new InstallTask(ver, currentFiles, this);

  task->onCommit([=] {
    m_receipt.addTicket(ticket);
    m_receipt.addRemovals(task->removedFiles());

    const Registry::Entry newEntry = m_registry->push(ver);

    if(newEntry.type == Package::ExtensionType)
      m_receipt.setRestartNeeded(true);

    registerInHost(true, newEntry);
  });

  addTask(task);
}

void Transaction::registerAll(const Remote &remote)
{
  const vector<Registry::Entry> &entries = m_registry->getEntries(remote.name());

  for(const auto &entry : entries)
    registerInHost(remote.isEnabled(), entry);

  if(!remote.isEnabled())
    inhibit(remote);
}

void Transaction::uninstall(const Remote &remote)
{
  inhibit(remote);

  const Path &indexPath = Index::pathFor(remote.name());

  if(FS::exists(indexPath)) {
    if(!FS::remove(indexPath))
      addError(FS::lastError(), indexPath.join());
  }

  const vector<Registry::Entry> &entries = m_registry->getEntries(remote.name());

  if(entries.empty())
    return;

  for(const auto &entry : entries)
    uninstall(entry);
}

void Transaction::uninstall(const Registry::Entry &entry)
{
  vector<Path> files;

  for(const Path &path : m_registry->getFiles(entry)) {
    if(FS::exists(path))
      files.push_back(path);
  }

  registerInHost(false, entry);

  // forget the package even if some files cannot be removed,
  // once the pre-run savepoint is released
  m_onRun.connect(bind(&Registry::forget, m_registry, entry));

  RemoveTask *task = new RemoveTask(files, this);
  task->onCommit([=] { m_receipt.addRemovals(task->removedFiles()); });

  addTask(task);
}

bool Transaction::saveFile(Download *dl, const Path &path)
{
  if(dl->state() != Download::Success) {
    addError(dl->contents(), dl->url());
    return false;
  }

  if(!FS::write(path, dl->contents())) {
    addError(FS::lastError(), path.join());
    return false;
  }

  return true;
}

void Transaction::finish()
{
  // called when the download queue is done, or if there is nothing to do

  if(!m_isCancelled) {
    for(Task *task : m_tasks)
      task->commit();

    m_registry->commit();

    registerQueued();
  }

  m_onFinish();
  m_cleanupHandler();
}

void Transaction::addError(const string &message, const string &title)
{
  m_receipt.addError({message, title});
  m_receipt.setEnabled(true);
}

bool Transaction::allFilesExists(const set<Path> &list) const
{
  for(const Path &path : list) {
    if(!FS::exists(path))
      return false;
  }

  return true;
}

void Transaction::addTask(Task *task)
{
  m_tasks.push_back(task);
  m_taskQueue.push(task);

  m_receipt.setEnabled(true);
}

void Transaction::runTasks()
{
  m_registry->restore();

  // execute code waiting for the savepoint to be released
  m_onRun();
  m_onRun.disconnect_all_slots();

  while(!m_installQueue.empty()) {
    installTicket(m_installQueue.front());
    m_installQueue.pop();
  }

  while(!m_taskQueue.empty()) {
    m_taskQueue.front()->start();
    m_taskQueue.pop();
  }

  m_registry->savepoint(); // get ready for new tasks

  if(m_downloadQueue.idle())
    finish();
}

void Transaction::registerInHost(const bool add, const Registry::Entry &entry)
{
  // don't actually do anything until commit() – which will calls registerQueued
  m_regQueue.push({add, entry, m_registry->getMainFile(entry)});
}

void Transaction::registerQueued()
{
  while(!m_regQueue.empty()) {
    const HostTicket &reg = m_regQueue.front();

    // don't register in host if the remote got disabled meanwhile
    if(reg.add && m_inhibited.count(reg.entry.remote) > 0) {
      m_regQueue.pop();
      return;
    }

    switch(reg.entry.type) {
    case Package::ScriptType:
      registerScript(reg);
      break;
    default:
      break;
    }

    m_regQueue.pop();
  }
}

void Transaction::registerScript(const HostTicket &reg)
{
  enum Section { MainSection = 0, MidiEditorSection = 32060 };

  if(!AddRemoveReaScript)
    return; // do nothing if REAPER < v5.12

  Section section;
  string category = Path(reg.entry.category).first();
  boost::algorithm::to_lower(category);

  if(category == "midi editor")
    section = MidiEditorSection;
  else
    section = MainSection;

  const std::string &fullPath = Path::prefixRoot(reg.file).join();
  const bool isLast = m_regQueue.size() == 1;

  if(!AddRemoveReaScript(reg.add, section, fullPath.c_str(), isLast) && reg.add)
    addError("Script could not be registered in REAPER.", reg.file);
}

void Transaction::inhibit(const Remote &remote)
{
  // prevents index post-download callbacks from being called
  // AND prevents files from this remote from being registered in REAPER
  // (UNregistering is not affected)

  const auto it = m_remotes.find(remote.name());
  if(it != m_remotes.end())
    m_remotes.erase(it);

  m_inhibited.insert(remote.name());
}
