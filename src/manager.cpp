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

#include "manager.hpp"

#include "about.hpp"
#include "config.hpp"
#include "encoding.hpp"
#include "errors.hpp"
#include "index.hpp"
#include "menu.hpp"
#include "reapack.hpp"
#include "remote.hpp"
#include "resource.hpp"

using namespace std;

enum { ACTION_ENABLE = 300, ACTION_DISABLE, ACTION_UNINSTALL, ACTION_ABOUT };

Manager::Manager(ReaPack *reapack)
  : Dialog(IDD_CONFIG_DIALOG), m_reapack(reapack), m_list(0)
{
}

void Manager::onInit()
{
  // It would be better to not hard-code the column sizes like that...
#ifndef _WIN32
  const int URL_WIDTH = 360;
#else
  const int URL_WIDTH = 300;
#endif

  m_list = createControl<ListView>(IDC_LIST, ListView::Columns{
    {AUTO_STR("Name"), 110},
    {AUTO_STR("URL"), URL_WIDTH},
    {AUTO_STR("State"), 60},
  });

  m_list->onActivate(bind(&Manager::about, this));
}

void Manager::onCommand(const int id)
{
  switch(id) {
  case IDC_IMPORT:
    m_reapack->importRemote();
    break;
  case ACTION_ENABLE:
    setRemoteEnabled(true);
    break;
  case ACTION_DISABLE:
    setRemoteEnabled(false);
    break;
  case ACTION_UNINSTALL:
    uninstall();
    break;
  case ACTION_ABOUT:
    about();
    break;
  case IDOK:
    if(confirm())
      apply();
    else {
      m_uninstall.clear();
      refresh();
      break;
    }
  case IDCANCEL:
    reset();
    close();
    break;
  }
}

void Manager::onContextMenu(HWND target, const int x, const int y)
{
  if(target != m_list->handle())
    return;

  const Remote &remote = currentRemote();

  if(remote.isNull())
    return;

  Menu menu;

  const UINT enableAction =
    menu.addAction(AUTO_STR("&Enable and synchronize"), ACTION_ENABLE);
  const UINT disableAction =
    menu.addAction(AUTO_STR("&Disable (keep files)"), ACTION_DISABLE);

  menu.addSeparator();

  const UINT uninstallAction =
    menu.addAction(AUTO_STR("&Uninstall"), ACTION_UNINSTALL);

  menu.addSeparator();

  auto_char aboutLabel[255] = {};
  const auto_string &name = make_autostring(remote.name());
  auto_snprintf(aboutLabel, sizeof(aboutLabel),
    AUTO_STR("&About %s..."), name.c_str());
  menu.addAction(aboutLabel, ACTION_ABOUT);

  menu.disable(enableAction);
  menu.disable(disableAction);
  menu.disable(uninstallAction);

  if(!remote.isNull()) {
    if(isRemoteEnabled(remote))
      menu.enable(disableAction);
    else
      menu.enable(enableAction);

    if(!remote.isProtected())
      menu.enable(uninstallAction);
  }

  menu.show(x, y, handle());
}

void Manager::refresh()
{
  InhibitControl lock(m_list);
  m_list->clear();

  for(const Remote &remote : *m_reapack->config()->remotes()) {
    if(!m_uninstall.count(remote))
      m_list->addRow(makeRow(remote));
  }

  m_list->sort();

#ifdef LVSCW_AUTOSIZE_USEHEADER
  m_list->resizeColumn(2, LVSCW_AUTOSIZE_USEHEADER);
#endif
}

void Manager::setRemoteEnabled(const bool enabled)
{
  const Remote &remote = currentRemote();

  if(remote.isNull())
    return;

  auto it = m_enableOverrides.find(remote);

  if(it == m_enableOverrides.end())
    m_enableOverrides.insert({remote, enabled});
  else if(remote.isEnabled() == enabled)
    m_enableOverrides.erase(it);
  else
    it->second = enabled;

  m_list->replaceRow(m_list->currentIndex(), makeRow(remote));
}

bool Manager::isRemoteEnabled(const Remote &remote) const
{
  const auto it = m_enableOverrides.find(remote);

  if(it == m_enableOverrides.end())
    return remote.isEnabled();
  else
    return it->second;
}

void Manager::uninstall()
{
  const Remote &remote = currentRemote();
  m_uninstall.insert(remote);

  const auto it = m_enableOverrides.find(remote);

  if(it != m_enableOverrides.end())
    m_enableOverrides.erase(it);

  m_list->removeRow(m_list->currentIndex());
}

void Manager::about()
{
  Remote remote = currentRemote();

  if(remote.isNull())
    return;

  // show the pending enable state changes as if they were already applied
  remote.setEnabled(isRemoteEnabled(remote));

  m_reapack->requireIndex(remote, bind(&Manager::showAbout, this, remote));
}

void Manager::showAbout(const Remote &remote)
{
  const RemoteIndex *index;

  try {
    index = RemoteIndex::load(remote.name());
  }
  catch(const reapack_error &e) {
    const auto_string &desc = make_autostring(e.what());

    auto_char msg[512] = {};
    auto_snprintf(msg, sizeof(msg),
      AUTO_STR("ReaPack could not read %s's index.\n\n")

      AUTO_STR("Synchronize your packages and try again.\n")
      AUTO_STR("If the problem persist, contact the repository maintainer.\n\n")

      AUTO_STR("[Error description: %s]"),
      make_autostring(remote.name()).c_str(), desc.c_str()
    );

    MessageBox(handle(), msg, AUTO_STR("ReaPack Warning"), MB_OK);
    return;
  }

  unique_ptr<const RemoteIndex> ptr(index);

  switch(Dialog::Show<About>(instance(), handle(), &remote, index)) {
  case About::EnableResult:
    setRemoteEnabled(true);
    break;
  }
}

bool Manager::confirm() const
{
  if(m_uninstall.empty())
    return true;

  const size_t uninstallSize = m_uninstall.size();

  auto_char msg[255] = {};
  auto_snprintf(msg, sizeof(msg),
    AUTO_STR("Uninstall %lu remote%s?\n")
    AUTO_STR("Every file they contain will be removed from your computer."),
    uninstallSize, uninstallSize == 1 ? AUTO_STR("") : AUTO_STR("s"));

  const auto_char *title = AUTO_STR("ReaPack Query");
  const int btn = MessageBox(handle(), msg, title, MB_YESNO);

  return btn == IDYES;
}

void Manager::apply()
{
  for(const auto &pair : m_enableOverrides) {
    const Remote &remote = pair.first;
    const bool enable = pair.second;

    if(enable)
      m_reapack->enable(remote);
    else
      m_reapack->disable(remote);
  }

  for(const Remote &remote : m_uninstall)
    m_reapack->uninstall(remote);

  m_reapack->config()->write();
  m_reapack->runTasks();
}

void Manager::reset()
{
  m_enableOverrides.clear();
  m_uninstall.clear();
}

ListView::Row Manager::makeRow(const Remote &remote) const
{
  const auto_string &name = make_autostring(remote.name());
  const auto_string &url = make_autostring(remote.url());

  return {name, url, isRemoteEnabled(remote) ?
    AUTO_STR("Enabled") : AUTO_STR("Disabled")};
}

Remote Manager::currentRemote() const
{
  const int index = m_list->currentIndex();

  if(index < 0)
    return {};

  const ListView::Row &row = m_list->row(index);
  const string &remoteName = from_autostring(row[0]);

  return m_reapack->config()->remotes()->get(remoteName);
}
