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

#include "download.hpp"

#include "filesystem.hpp"
#include "reapack.hpp"

#include <boost/format.hpp>

#include <reaper_plugin_functions.h>

using boost::format;
using namespace std;

static const int DOWNLOAD_TIMEOUT = 15;
// to set the amount of concurrent downloads, change the size of
// the m_pool member in ThreadPool (thread.hpp)

static CURLSH *g_curlShare = nullptr;
static WDL_Mutex g_curlMutex;

static void LockCurlMutex(CURL *, curl_lock_data, curl_lock_access, void *)
{
  g_curlMutex.Enter();
}

static void UnlockCurlMutex(CURL *, curl_lock_data, curl_lock_access, void *)
{
  g_curlMutex.Leave();
}

void DownloadContext::GlobalInit()
{
  curl_global_init(CURL_GLOBAL_DEFAULT);

  g_curlShare = curl_share_init();
  assert(g_curlShare);

  curl_share_setopt(g_curlShare, CURLSHOPT_LOCKFUNC, LockCurlMutex);
  curl_share_setopt(g_curlShare, CURLSHOPT_UNLOCKFUNC, UnlockCurlMutex);

  curl_share_setopt(g_curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
  curl_share_setopt(g_curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
}

void DownloadContext::GlobalCleanup()
{
  curl_share_cleanup(g_curlShare);
  curl_global_cleanup();
}

DownloadContext::DownloadContext()
{
  m_curl = curl_easy_init();

  const auto userAgent = format("ReaPack/%s REAPER/%s")
    % ReaPack::VERSION % GetAppVersion();

  curl_easy_setopt(m_curl, CURLOPT_USERAGENT, userAgent.str().c_str());
  curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 1);
  curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, DOWNLOAD_TIMEOUT);
  curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, DOWNLOAD_TIMEOUT);
  curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, true);
  curl_easy_setopt(m_curl, CURLOPT_MAXREDIRS, 5);
  curl_easy_setopt(m_curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(m_curl, CURLOPT_FAILONERROR, true);
  curl_easy_setopt(m_curl, CURLOPT_SHARE, g_curlShare);
  curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, false);
}

DownloadContext::~DownloadContext()
{
  curl_easy_cleanup(m_curl);
}

size_t Download::WriteData(char *data, size_t rawsize, size_t nmemb, void *ptr)
{
  const size_t size = rawsize * nmemb;

  static_cast<ostream *>(ptr)->write(data, size);

  return size;
}

int Download::UpdateProgress(void *ptr, const double, const double,
    const double, const double)
{
  return static_cast<Download *>(ptr)->aborted();
}

Download::Download(const string &url, const NetworkOpts &opts, const int flags)
  : m_url(url), m_opts(opts), m_flags(flags), m_ctx(nullptr)
{
}

void Download::setName(const string &name)
{
  setSummary("Downloading %s: " + name);
}

bool Download::run()
{
  ostream *stream = openStream();
  if(!stream)
    return false;

  curl_easy_setopt(m_ctx->m_curl, CURLOPT_URL, m_url.c_str());
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_PROXY, m_opts.proxy.c_str());
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_SSL_VERIFYPEER, m_opts.verifyPeer);

  curl_easy_setopt(m_ctx->m_curl, CURLOPT_PROGRESSFUNCTION, UpdateProgress);
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_PROGRESSDATA, this);

  curl_easy_setopt(m_ctx->m_curl, CURLOPT_WRITEFUNCTION, WriteData);
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_WRITEDATA, stream);

  curl_slist *headers = nullptr;
  if(has(Download::NoCacheFlag))
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_HTTPHEADER, headers);

  char errbuf[CURL_ERROR_SIZE] = "No error message";
  curl_easy_setopt(m_ctx->m_curl, CURLOPT_ERRORBUFFER, errbuf);

  const CURLcode res = curl_easy_perform(m_ctx->m_curl);
  curl_slist_free_all(headers);
  closeStream();

  if(res != CURLE_OK) {
    const auto err = format("%s (%d): %s") % curl_easy_strerror(res) % res % errbuf;
    setError({err.str(), m_url});
    return false;
  }

  return true;
}

MemoryDownload::MemoryDownload(const string &url, const NetworkOpts &opts, int flags)
  : Download(url, opts, flags)
{
  setName(url);
}

FileDownload::FileDownload(const Path &target, const string &url,
    const NetworkOpts &opts, int flags)
  : Download(url, opts, flags), m_path(target)
{
  setName(target.join());
}

bool FileDownload::save()
{
  if(state() == Success)
    return FS::rename(m_path);
  else
    return FS::remove(m_path.temp());
}

ostream *FileDownload::openStream()
{
  if(FS::open(m_stream, m_path.temp()))
    return &m_stream;
  else {
    setError({FS::lastError(), m_path.temp().join()});
    return nullptr;
  }
}

void FileDownload::closeStream()
{
  m_stream.close();
}
