/*
 * http.c : GeeXboX uShare Web Server handler.
 * Originally developped for the GeeXboX project.
 * Parts of the code are originated from GMediaServer from Oskar Liljeblad.
 * Copyright (C) 2005-2007 Benjamin Zores <ben@geexbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdafx.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <wchar.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#include <upnp/upnp.h>
#include <upnp/upnptools.h>

#include "services.h"
#include "cds.h"
#include "cms.h"
#include "msr.h"
#include "metadata.h"
#include "http.h"
#include "minmax.h"
#include "trace.h"
#include "presentation.h"
#include "osdep.h"
#include "mime.h"

#define PROTOCOL_TYPE_PRE_SZ  11   /* for the str length of "http-get:*:" */
#define PROTOCOL_TYPE_SUFF_SZ 2    /* for the str length of ":*" */


#ifdef _WIN32
bool httpGetDataFile_char(IN char const * const strFilename, OUT wchar_t const **const wstrFilePath)
{
	 wchar_t * wstrFilename = (wchar_t *) malloc(PATH_MAX * sizeof(wchar_t));
	 _snwprintf(wstrFilename,PATH_MAX,L"%hs",strFilename);

	 {
	 bool b = httpGetDataFile(wstrFilename,wstrFilePath);
	 free(wstrFilename);
	 return b;
	 }
}


bool httpGetDataFile(IN wchar_t const * const wstrFilename, OUT wchar_t const **const wstrFilePath)
{
	wchar_t * wstrFilePathBuff = (wchar_t *) malloc(PATH_MAX * sizeof(wchar_t));

	static wchar_t const * s_http_wstrAppDataPath = NULL;



	if (NULL == wstrFilename) return false;

	if (NULL == s_http_wstrAppDataPath)
	{
		Windows_GetAppDataFolderFromRegistry(&s_http_wstrAppDataPath);
		Windows_ExpandEnvironmentStrings(s_http_wstrAppDataPath,&s_http_wstrAppDataPath);
	}

	_snwprintf(wstrFilePathBuff,PATH_MAX,L"%s\\%hs\\%s\\%s",s_http_wstrAppDataPath,SYSCONFDIR,L"data",wstrFilename);
	*wstrFilePath = (wchar_t *) realloc(wstrFilePathBuff,PATH_MAX * sizeof(wchar_t)); // set it.

	return true;
}
#else
#endif


struct web_file_t {
  char *fullpath;
  ssize_t pos;
  enum { FILE_LOCAL, FILE_MEMORY } type;
  union {
    struct {
#ifdef _MSC_VER
      FILE* fd;
	  HANDLE fileMapping;
	  __int64 fileSize;
#else
      int fd;
#endif
      struct upnp_entry_t *entry;
    } local;
    struct {
      char *contents;
      ssize_t len;
    } memory;
  } detail;
};


static _inline void
set_info_file (IN UpnpFileInfo *info, const ssize_t length,
               const char *content_type)
{
  UpnpFileInfo_set_FileLength(info,length);
  UpnpFileInfo_set_LastModified(info,0);
  UpnpFileInfo_set_IsDirectory(info,0);
  UpnpFileInfo_set_IsReadable(info,1);
  UpnpFileInfo_set_ContentType(info,content_type);
}


int http_getVirtualInfo(OUT UpnpFileInfo *info, char const * const strFilename, char const * const strFileMime)
{
#ifdef _WIN32
	  wchar_t const * wstrFilepath = NULL;

	  httpGetDataFile_char(strFilename,&wstrFilepath);
	  {
	  struct _stat64 st;
	  _wstat64 (wstrFilepath, &st);
	  set_info_file (info, st.st_size, strFileMime);
	  }
	  return 0;
#else
	  struct  _stat64 st;
	   _stat64 (ICON_FILE_SM_PNG, &st);
	  set_info_file (info, st.st_size, ICON_MIME_PNG);
	  return 0;
#endif
}


static int
http_get_info (const char *filename, OUT UpnpFileInfo *info)
{
  extern struct ushare_t *ut;
  struct upnp_entry_t *entry = NULL;
  struct  _stat64 st;
  int upnp_id = 0;
  char *content_type = NULL;
  char *protocol = NULL;
  
  if (!filename || !info)
    return -1;

  if (ut->verbose) log_verbose ("http_get_info, filename : %s\n", filename);

  if (!strcmp (filename, ICON_LOCATION_SM_PNG))
  {
	  return http_getVirtualInfo(info,ICON_FILE_SM_PNG,ICON_MIME_PNG);
  }

  if (!strcmp (filename, ICON_LOCATION_SM_JPEG))
  {
	  return http_getVirtualInfo(info,ICON_FILE_SM_JPEG,ICON_MIME_JPEG);
  }

  if (!strcmp (filename, ICON_LOCATION_LRG_PNG))
  {
	  return http_getVirtualInfo(info,ICON_FILE_LRG_PNG,ICON_MIME_PNG);
  }

  if (!strcmp (filename, ICON_LOCATION_LRG_JPEG))
  {
	  return http_getVirtualInfo(info,ICON_FILE_LRG_JPEG,ICON_MIME_JPEG);
  }

  if (!strcmp (filename, CDS_LOCATION))
  {
    set_info_file (info, CDS_DESCRIPTION_LEN, SERVICE_CONTENT_TYPE);
    return 0;
  }

  if (!strcmp (filename, CMS_LOCATION))
  {
    set_info_file (info, CMS_DESCRIPTION_LEN, SERVICE_CONTENT_TYPE);
    return 0;
  }

  if (!strcmp (filename, MSR_LOCATION))
  {
    set_info_file (info, MSR_DESCRIPTION_LEN, SERVICE_CONTENT_TYPE);
    return 0;
  }

  if (ut->use_presentation && !strcmp (filename, USHARE_PRESENTATION_PAGE))
  {
    if (build_presentation_page (ut) < 0)
      return -1;

    set_info_file (info, ut->presentation->len, PRESENTATION_PAGE_CONTENT_TYPE);
    return 0;
  }

  if (ut->use_presentation && !strncmp (filename, USHARE_CGI, strlen (USHARE_CGI)))
  {
    if (process_cgi (ut, (char *) (filename + strlen (USHARE_CGI) + 1)) < 0)
      return -1;

   set_info_file (info, ut->presentation->len, PRESENTATION_PAGE_CONTENT_TYPE);
    return 0;
  }

  upnp_id = atoi (strrchr (filename, '/') + 1);
  entry = upnp_get_entry (ut, upnp_id);
  if (!entry)
    return -1;

  if (!entry->fullpath)
    return -1;

  if ( _stat64 (entry->fullpath, &st) < 0)
    return -1;

#ifndef _MSC_VER
  if (access (entry->fullpath, R_OK) < 0)
  {
    if (errno != EACCES)
      return -1;
	UpnpFileInfo_set_IsReadable(info,0);
  }
  else
#endif
    UpnpFileInfo_set_IsReadable(info,1);

  /* file exist and can be read */
  UpnpFileInfo_set_FileLength(info,st.st_size);
  UpnpFileInfo_set_LastModified(info,st.st_mtime);
  UpnpFileInfo_set_IsDirectory(info,S_ISDIR (st.st_mode));

  protocol = 
#ifdef HAVE_DLNA
    entry->dlna_profile ?
    dlna_write_protocol_info (DLNA_PROTOCOL_INFO_TYPE_HTTP,
                              DLNA_ORG_PLAY_SPEED_NORMAL,
                              DLNA_ORG_CONVERSION_NONE,
                              DLNA_ORG_OPERATION_RANGE,
                              ut->dlna_flags, entry->dlna_profile) :
#endif /* HAVE_DLNA */
    mime_get_protocol (entry->mime_type);

  content_type =
    strndup ((protocol + PROTOCOL_TYPE_PRE_SZ),
             strlen (protocol + PROTOCOL_TYPE_PRE_SZ)
             - PROTOCOL_TYPE_SUFF_SZ);
  free (protocol);

  if (content_type)
  {
	  UpnpFileInfo_set_ContentType(info,ixmlCloneDOMString(content_type));
    free (content_type);
  }
  else
	  UpnpFileInfo_set_ContentType(info,ixmlCloneDOMString (""));

  return 0;
}

static UpnpWebFileHandle
get_file_memory (const char *fullpath, const char *description,
                 const size_t length)
{
  struct web_file_t *file;

  file = malloc (sizeof (struct web_file_t));
  file->fullpath = _strdup (fullpath);
  file->pos = 0;
  file->type = FILE_MEMORY;
  file->detail.memory.contents = _strdup (description);
  file->detail.memory.len = length;

  return ((UpnpWebFileHandle) file);
}

static UpnpWebFileHandle
get_file_local (const char *fullpath)
{
  struct web_file_t *file;
  FILE * fd = NULL;
  errno_t err = 0;

#ifdef _WIN32
  wchar_t const * http_fullpathex = NULL;

  httpGetDataFile_char(fullpath,&http_fullpathex);
#else
#endif

  log_verbose ("Fullpath : %s\n", fullpath);

#ifdef _WIN32
  err = _wfopen_s (&fd,http_fullpathex, L"rb" );
  if (fd < 0)
    return NULL;
#else

  fd = open (fullpath, O_RDONLY | O_NONBLOCK | O_SYNC | O_NDELAY);
  if (fd < 0)
    return NULL;
#endif

  file = malloc (sizeof (struct web_file_t));
  file->fullpath = _strdup (fullpath);
  file->pos = 0;
  file->type = FILE_LOCAL;
  file->detail.local.entry = NULL;
  file->detail.local.fd = fd;

  return ((UpnpWebFileHandle) file);
}

static UpnpWebFileHandle
http_open (const char *filename, enum UpnpOpenFileMode mode)
{
  extern struct ushare_t *ut;
  struct upnp_entry_t *entry = NULL;
  struct web_file_t *file  = NULL;
  FILE *fd = NULL;
  int upnp_id = 0;
  errno_t err = 0;

  if (!filename)
    return NULL;

  if (ut->verbose) log_verbose ("http_open, filename : %s\n", filename);

  if (mode != UPNP_READ)
    return NULL;

  if (!strcmp (filename, ICON_LOCATION_SM_PNG))
    return get_file_local (ICON_FILE_SM_PNG);

  if (!strcmp (filename, ICON_LOCATION_LRG_PNG))
    return get_file_local (ICON_FILE_LRG_PNG);

  if (!strcmp (filename, ICON_LOCATION_SM_JPEG))
    return get_file_local (ICON_FILE_SM_JPEG);

  if (!strcmp (filename, ICON_LOCATION_LRG_JPEG))
    return get_file_local (ICON_FILE_LRG_JPEG);

  if (!strcmp (filename, CDS_LOCATION))
    return get_file_memory (CDS_LOCATION, CDS_DESCRIPTION, CDS_DESCRIPTION_LEN);

  if (!strcmp (filename, CMS_LOCATION))
    return get_file_memory (CMS_LOCATION, CMS_DESCRIPTION, CMS_DESCRIPTION_LEN);

  if (!strcmp (filename, MSR_LOCATION))
    return get_file_memory (MSR_LOCATION, MSR_DESCRIPTION, MSR_DESCRIPTION_LEN);

  if (ut->use_presentation && ( !strcmp (filename, USHARE_PRESENTATION_PAGE)
      || !strncmp (filename, USHARE_CGI, strlen (USHARE_CGI))))
    return get_file_memory (USHARE_PRESENTATION_PAGE, ut->presentation->buf,
                            ut->presentation->len);

  upnp_id = atoi (strrchr (filename, '/') + 1);
  entry = upnp_get_entry (ut, upnp_id);
  if (!entry)
    return NULL;

  if (!entry->fullpath)
    return NULL;

  if (NULL == ut->strNowPlaying || strcmp(ut->strNowPlaying,entry->fullpath))
  {
	  if (NULL != ut->strNowPlaying) free (ut->strNowPlaying);
	  {
	  size_t lenFullpath = (strlen(entry->fullpath) +1) * sizeof(char *);
	  char * strBufNowPlaying = (char *) malloc(lenFullpath);
	  ut->strNowPlaying = (char *) memcpy(strBufNowPlaying,entry->fullpath,lenFullpath);
	  }
	log_info ("Now Playing: %s\n", ut->strNowPlaying);
  }

  log_verbose ("Opening File: %s\n", entry->fullpath);

#ifdef _WIN32
  {
	  wchar_t * wFilename = (wchar_t *) malloc((PATH_MAX+1)*sizeof(wchar_t*));
	  _snwprintf(wFilename,PATH_MAX,L"%hs",entry->fullpath);
	  err = _wfopen_s (&fd,wFilename, L"rb" );
	  if (fd < 0)
		  return NULL;
  }
#else
  fd = open (entry->fullpath, O_RDONLY | O_NONBLOCK | O_SYNC | O_NDELAY);
  if (fd < 0)
    return NULL;
#endif

  file = malloc (sizeof (struct web_file_t));
  file->fullpath = _strdup (entry->fullpath);
  file->pos = 0;
  file->type = FILE_LOCAL;
  file->detail.local.entry = entry;
  file->detail.local.fd = fd;

  return ((UpnpWebFileHandle) file);
}

static int
http_read (UpnpWebFileHandle fh, char *buf, size_t buflen)
{
  struct web_file_t *file = (struct web_file_t *) fh;
  ssize_t len = -1;

  log_verbose ("http_read\n");

  if (!file)
    return -1;

  switch (file->type)
  {
  case FILE_LOCAL:
    log_verbose ("Read local file.\n");
#ifdef _WIN32
    len = fread (buf, sizeof(char), buflen,file->detail.local.fd);
#else
    len = _read (file->detail.local.fd, buf, buflen);
#endif

    break;
  case FILE_MEMORY:
    log_verbose ("Read file from memory.\n");
    len = MIN ((size_t) buflen, (size_t) (file->detail.memory.len - file->pos));
    memcpy (buf, file->detail.memory.contents + file->pos, (size_t) len);
    break;
  default:
    log_verbose ("Unknown file type.\n");
    break;
  }

  if (len >= 0)
    file->pos += len;

  log_verbose ("Read %lld bytes.\n", len);

  if (len < 0) return -1;
  else return (int)len;
}

#ifdef _WIN32
static int
http_write (UpnpWebFileHandle fh,
            char *buf,
            size_t buflen)
#else
static int
http_write (UpnpWebFileHandle fh __attribute__((unused)),
            char *buf __attribute__((unused)),
            size_t buflen __attribute__((unused)))
#endif
{
  log_verbose ("http write\n");

  return 0;
}

static int
http_seek (UpnpWebFileHandle fh, ptrdiff_t offset, int origin)
{
  struct web_file_t *file = (struct web_file_t *) fh;
  ssize_t newpos = -1;

  log_verbose ("http_seek\n");

  if (!file)
    return -1;

  switch (origin)
  {
  case SEEK_SET:
    log_verbose ("Attempting to seek to %lld (was at %ld) in %s\n",
                offset, file->pos, file->fullpath);
    newpos = offset;
    break;
  case SEEK_CUR:
    log_verbose ("Attempting to seek by %lld from %ld in %s\n",
                offset, file->pos, file->fullpath);
    newpos = file->pos + offset;
    break;
  case SEEK_END:
    log_verbose ("Attempting to seek by %lld from end (was at %ld) in %s\n",
                offset, file->pos, file->fullpath);

    if (file->type == FILE_LOCAL)
    {
      struct  _stat64 sb;
      if ( _stat64 (file->fullpath, &sb) < 0)
      {
        log_verbose ("%s: cannot  _stat64: %s\n",
                    file->fullpath, strerror (errno));
        return -1;
      }
      newpos = sb.st_size + offset;
    }
    else if (file->type == FILE_MEMORY)
      newpos = file->detail.memory.len + offset;
    break;
  }

  switch (file->type)
  {
  case FILE_LOCAL:
    /* Just make sure we cannot seek before start of file. */
    if (newpos < 0)
    {
      log_verbose ("%s: cannot seek: %s\n", file->fullpath, strerror (EINVAL));
      return -1;
    }

    /* Don't seek with origin as specified above, as file may have
       changed in size since our last  _stat64. */
#ifdef _WIN32
    if (_fseeki64 (file->detail.local.fd, newpos, SEEK_SET) == -1)
#else
    if (_lseek (file->detail.local.fd, newpos, SEEK_SET) == -1)
#endif
    {
      log_verbose ("%s: cannot seek: %s\n", file->fullpath, strerror (errno));
      return -1;
    }
    break;
  case FILE_MEMORY:
    if (newpos < 0 || newpos > file->detail.memory.len)
    {
      log_verbose ("%s: cannot seek: %s\n", file->fullpath, strerror (EINVAL));
      return -1;
    }
    break;
  }

  file->pos = newpos;

  return 0;
}

static int
http_close (UpnpWebFileHandle fh)
{
  struct web_file_t *file = (struct web_file_t *) fh;

  log_verbose ("http_close\n");

  if (!file)
    return -1;

  switch (file->type)
  {
  case FILE_LOCAL:
#ifdef _WIN32
    fclose (file->detail.local.fd);
#else
    _close (file->detail.local.fd);
#endif
	break;
  case FILE_MEMORY:
    /* no close operation */
    if (file->detail.memory.contents)
      free (file->detail.memory.contents);
    break;
  default:
    log_verbose ("Unknown file type.\n");
    break;
  }

  if (file->fullpath)
    free (file->fullpath);
  free (file);

  return 0;
}

void http_setcallbaks(){

	  UpnpVirtualDir_set_GetInfoCallback(&http_get_info);
	  UpnpVirtualDir_set_OpenCallback(&http_open);
	  UpnpVirtualDir_set_ReadCallback(&http_read);
	  UpnpVirtualDir_set_WriteCallback(&http_write);
	  UpnpVirtualDir_set_SeekCallback(&http_seek);
	  UpnpVirtualDir_set_CloseCallback(&http_close);
};


//struct VirtualDirCallbacks virtual_dir_callbacks = {
//  http_get_info,
//  http_open,
//  http_read,
//  http_write,
//  http_seek,
//  http_close
//};
