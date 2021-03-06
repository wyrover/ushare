/*
 * metadata.c : GeeXboX uShare CDS Metadata DB.
 * Originally developped for the GeeXboX project.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>



#include <upnp/upnp.h>
#include <upnp/upnptools.h>

#include "mime.h"
#include "metadata.h"
#include "util_iconv.h"
#include "content.h"
#include "gettext.h"
#include "trace.h"

#ifdef HAVE_FAM
#include "ufam.h"
#endif /* HAVE_FAM */

#define TITLE_UNKNOWN "unknown"

#define MAX_URL_SIZE 32

struct upnp_entry_lookup_t {
  int id;
  struct upnp_entry_t *entry_ptr;
};

struct upnp_entry_lookup_name_t {
  char *path;
  struct upnp_entry_t *entry_ptr;
};

#ifdef _WIN32
static int
metadata_add_file (struct ushare_t *ut, struct upnp_entry_t *entry,
                   const char *file, const char *name, struct _stat64 *st_ptr);
#else
static int
metadata_add_file (struct ushare_t *ut, struct upnp_entry_t *entry,
                   const char *file, const char *name, struct  _stat64 *st_ptr);
#endif

static char *
getExtension (const char *filename)
{
  char *str = NULL;

  str = strrchr (filename, '.');
  if (str)
    str++;

  return str;
}

static struct mime_type_t *
getMimeType (const char *extension)
{
  extern struct mime_type_t MIME_Type_List[];
  struct mime_type_t *list;

  if (!extension)
    return NULL;

  list = MIME_Type_List;
  while (list->extension)
  {
    if (!strcasecmp (list->extension, (char *)extension))
      return list;
    list++;
  }

  return NULL;
}

static bool
is_valid_extension (const char *extension)
{
  if (!extension)
    return false;

  if (getMimeType (extension))
    return true;

  return false;
}

static int
get_list_length (void *list)
{
  void **l = list;
  int n = 0;

  while (*(l++))
    n++;

  return n;
}

static xml_convert_t xml_convert[] = {
  {'"' , "&quot;"},
  {'&' , "&amp;"},
  {'\'', "&apos;"},
  {'<' , "&lt;"},
  {'>' , "&gt;"},
  {'\n', "&#xA;"},
  {'\r', "&#xD;"},
  {'\t', "&#x9;"},
  {0, NULL},
};

static char *
get_xmlconvert (int c)
{
  int j;
  for (j = 0; xml_convert[j].xml; j++)
  {
    if (c == xml_convert[j].charac)
      return xml_convert[j].xml;
  }
  return NULL;
}

static char *
convert_xml (const char *title)
{
  char *newtitle, *s, *t, *xml;
  int nbconvert = 0;

  /* calculate extra size needed */
  for (t = (char*) title; *t; t++)
  {
    xml = get_xmlconvert (*t);
    if (xml)
      nbconvert += strlen (xml) - 1;
  }
  if (!nbconvert)
    return NULL;

  newtitle = s = (char*) malloc (strlen (title) + nbconvert + 1);

  for (t = (char*) title; *t; t++)
  {
    xml = get_xmlconvert (*t);
    if (xml)
    {
      strcpy (s, xml);
      s += strlen (xml);
    }
    else
      *s++ = *t;
  }
  *s = '\0';

  return newtitle;
}

static struct mime_type_t Container_MIME_Type =
  { NULL, "object.container.storageFolder", NULL};

#define ARRAY_NB_ELEMENTS(array) (sizeof (array) / sizeof (array[0]))

static int
vh_file_exists (const char *file)
{
  struct  _stat64 st;
  return ! _stat64 (file, &st);
}

static int
upnp_audio_get_cover (struct ushare_t *ut, struct upnp_entry_t *entry,
                      const char *fullpath, const char *class)
{
  const char *known_audio_filenames[] =
    { "cover", "COVER", "front", "FRONT" };

  const char *known_audio_extensions[] =
    { "jpg", "JPG", "jpeg", "JPEG", "png", "PNG", "tbn", "TBN" };

  char *s, *dir = NULL, *file = NULL, *cv = NULL, *f = NULL;
  unsigned int i, j;

#ifdef _WIN32
  struct _stat64 st;
#else
  struct  _stat64 st;
#endif

  int cover_id;

  if (!class || strcmp (class, UPNP_AUDIO))
    goto end;

  /* retrieve directory name */
  s = strrchr (fullpath, '/');
  if (!s)
    goto end;
  dir = strndup (fullpath, strlen (fullpath) - strlen (s));

  /* retrieve file base name */
  s = strrchr (fullpath, '.');
  if (!s)
    goto end;
  file = strndup (fullpath + strlen (dir) + 1,
                  strlen (fullpath) - strlen (dir) - strlen (s) - 1);

  /* try to find a generic cover file for the whole directory */
  for (i = 0; i < ARRAY_NB_ELEMENTS (known_audio_extensions); i++)
    for (j = 0; j < ARRAY_NB_ELEMENTS (known_audio_filenames); j++)
    {
      char cover[1024] = { 0 };
      char f2[512] = { 0 };

      snprintf (f2, sizeof (f2), "%s.%s",
                known_audio_filenames[j], known_audio_extensions[i]);
      snprintf (cover, sizeof (cover), "%s/%s", dir, f2);

      if (vh_file_exists (cover))
      {
        cv = _strdup (cover);
        f = _strdup (f2);
        goto end;
      }
    }

end:
  //printf ("Path: %s\n", fullpath);
  //printf (" Cover: %s\n", cv);

#ifdef _WIN32
	{
		wchar_t * wFilename = (wchar_t *) malloc((PATH_MAX+1)*sizeof(wchar_t*));
		_snwprintf(wFilename,PATH_MAX,L"%hs",fullpath);
		_wstat64 (wFilename, &st);
	}
#else
	 _stat64 (fullpath, &st);
#endif

  cover_id = metadata_add_file (ut, entry, cv, f, &st);
  //printf (" Cover ID: %d\n", cover_id);

  if (dir)
    free (dir);
  if (file)
    free (file);
  if (f)
    free (f);
  if (cv)
    free (cv);

  return cover_id;
}

static struct upnp_entry_t *
upnp_entry_new (struct ushare_t *ut, const char *name, const char *fullpath,
                struct upnp_entry_t *parent, ssize_t size, int dir)
{
  struct upnp_entry_t *entry = NULL;
  char *title = NULL, *x = NULL;
  char url_tmp[MAX_URL_SIZE] = { '\0' };
  char *title_or_name = NULL;

  if (!name)
    return NULL;

  entry = (struct upnp_entry_t *) malloc (sizeof (struct upnp_entry_t));

#ifdef HAVE_DLNA
  entry->dlna_profile = NULL;
  entry->url = NULL;
  if (ut->dlna_enabled && fullpath && !dir)
  {
    dlna_profile_t *p = dlna_guess_media_profile (ut->dlna, fullpath);
    if (!p)
    {
      free (entry);
      return NULL;
    }
    entry->dlna_profile = p;
  }
#endif /* HAVE_DLNA */
 
  if (ut->xbox360)
  {
    if (ut->root_entry)
      entry->id = ut->starting_id + ut->nr_entries++;
    else
      entry->id = 0; /* Creating the root node so don't use the usual IDs */
  }
  else
    entry->id = ut->starting_id + ut->nr_entries++;
  
  entry->fullpath = fullpath ? _strdup (fullpath) : NULL;
  entry->parent = parent;
  entry->child_count =  dir ? 0 : -1;
  entry->title = NULL;
#ifdef HAVE_FAM
  entry->ufam_entry = NULL;
#endif /* HAVE_FAM */

  entry->childs = (struct upnp_entry_t **)
    malloc (sizeof (struct upnp_entry_t *));
  *(entry->childs) = NULL;

  if (!dir) /* item */
    {
#ifdef HAVE_DLNA
      if (ut->dlna_enabled)
        entry->mime_type = NULL;
      else
      {
#endif /* HAVE_DLNA */
      struct mime_type_t *mime = getMimeType (getExtension (name));
      if (!mime)
      {
        --ut->nr_entries; 
        upnp_entry_free (ut, entry);
        log_error ("Invalid Mime type for %s, entry ignored", name);
        return NULL;
      }
      entry->mime_type = mime;
#ifdef HAVE_DLNA
      }
#endif /* HAVE_DLNA */
      
      if (snprintf (url_tmp, MAX_URL_SIZE, "%d.%s",
                    entry->id, getExtension (name)) >= MAX_URL_SIZE)
        log_error ("URL string too long for id %d, truncated!!", entry->id);

      /* look for audio album cover */
      upnp_audio_get_cover (ut, parent,
                            fullpath, entry->mime_type->mime_class);

      /* Only malloc() what we really need */
      entry->url = _strdup (url_tmp);
    }
  else /* container */
    {
      entry->mime_type = &Container_MIME_Type;
      entry->url = NULL;
#ifdef HAVE_FAM
      entry->ufam_entry = ufam_add_monitor (ut->ufam, entry);
#endif /* HAVE_FAM */
    }

  /* Try Iconv'ing the name but if it fails the end device
     may still be able to handle it */
  title = iconv_convert (name);
  if (title)
    title_or_name = title;
  else
  {
    if (ut->override_iconv_err)
    {
      title_or_name = _strdup (name);
      log_error ("Entry invalid name id=%d [%s]\n", entry->id, name);
    }
    else
    {
      upnp_entry_free (ut, entry);
      log_error ("Freeing entry invalid name id=%d [%s]\n", entry->id, name);
      return NULL;
    }
  }

  if (!dir)
  {
    x = strrchr (title_or_name, '.');
    if (x)  /* avoid displaying file extension */
      *x = '\0';
  }
  x = convert_xml (title_or_name);
  if (x)
  {
    free (title_or_name);
    title_or_name = x;
  }
  entry->title = title_or_name;

  if (!strcmp (title_or_name, "")) /* DIDL dc:title can't be empty */
  {
    free (title_or_name);
    entry->title = _strdup (TITLE_UNKNOWN);
  }

  entry->size = size;
  entry->fd = -1;

  if (entry->id && entry->url)
  {
	  if (ut->verbose)
   if (ut->verbose) log_verbose ("Entry->URL (%d): %s\n", entry->id, entry->url);
  }

  return entry;
}

/* Seperate recursive free() function in order to avoid freeing off
 * the parents child list within the freeing of the first child, as
 * the only entry which is not part of a childs list is the root entry
 */
static void
_upnp_entry_free (struct upnp_entry_t *entry)
{
  struct upnp_entry_t **childs;

  if (!entry)
    return;

  if (entry->fullpath)
    free (entry->fullpath);
  if (entry->title)
    free (entry->title);
  if (entry->url)
    free (entry->url);
#ifdef HAVE_DLNA
  if (entry->dlna_profile)
    entry->dlna_profile = NULL;
#endif /* HAVE_DLNA */
#ifdef HAVE_FAM
  if (entry->ufam_entry)
    ufam_remove_monitor (entry->ufam_entry);
#endif /* HAVE_FAM */

  for (childs = entry->childs; *childs; childs++)
    _upnp_entry_free (*childs);
  free (entry->childs);
}

void
upnp_entry_free (struct ushare_t *ut, struct upnp_entry_t *entry)
{
  if (!ut || !entry)
    return;

  /* Free all entries (i.e. children) */
  if (entry == ut->root_entry)
  {
    struct upnp_entry_t *entry_found = NULL;
    struct upnp_entry_lookup_t *lk = NULL;
    RBLIST *rblist;
    int i = 0;

    rblist = rbopenlist (ut->rb);
    lk = (struct upnp_entry_lookup_t *) rbreadlist (rblist);

    while (lk)
    {
      entry_found = lk->entry_ptr;
      if (entry_found)
      {
 	if (entry_found->fullpath)
 	  free (entry_found->fullpath);
 	if (entry_found->title)
 	  free (entry_found->title);
 	if (entry_found->url)
 	  free (entry_found->url);
#ifdef HAVE_FAM
        if (entry_found->ufam_entry)
          ufam_remove_monitor (entry_found->ufam_entry);
#endif /* HAVE_FAM */

	free (entry_found);
 	i++;
      }

      free (lk); /* delete the lookup */
      lk = (struct upnp_entry_lookup_t *) rbreadlist (rblist);
    }

    rbcloselist (rblist);
    rbdestroy (ut->rb);
    ut->rb = NULL;

    if (ut->verbose) log_verbose ("Freed [%d] entries\n", i);
  }
  else
    _upnp_entry_free (entry);

  free (entry);
}

static void
upnp_entry_add_child (struct ushare_t *ut,
                      struct upnp_entry_t *entry, struct upnp_entry_t *child)
{
  struct upnp_entry_lookup_t *entry_lookup_ptr = NULL;
  struct upnp_entry_t **childs;
  int n;

  if (!entry || !child)
    return;

  for (childs = entry->childs; *childs; childs++)
    if (*childs == child)
      return;

  n = get_list_length ((void *) entry->childs) + 1;
  entry->childs = (struct upnp_entry_t **)
    realloc (entry->childs, (n + 1) * sizeof (*(entry->childs)));
  entry->childs[n] = NULL;
  entry->childs[n - 1] = child;
  entry->child_count++;

  entry_lookup_ptr = (struct upnp_entry_lookup_t *)
    malloc (sizeof (struct upnp_entry_lookup_t));
  entry_lookup_ptr->id = child->id;
  entry_lookup_ptr->entry_ptr = child;

  if (rbsearch ((void *) entry_lookup_ptr, ut->rb) == NULL)
    log_info (_("Failed to add the RB lookup tree\n"));
}

struct upnp_entry_t *
upnp_get_entry (struct ushare_t *ut, int id)
{
  struct upnp_entry_lookup_t *res, entry_lookup;

  if (ut->verbose) log_verbose ("Looking for entry id %d\n", id);
  if (id == 0) /* We do not store the root (id 0) as it is not a child */
    return ut->root_entry;

  entry_lookup.id = id;
  res = (struct upnp_entry_lookup_t *)
    rbfind ((void *) &entry_lookup, ut->rb);

  if (res)
  {
    if (ut->verbose) log_verbose ("Found at %p\n",
                 ((struct upnp_entry_lookup_t *) res)->entry_ptr);
    return ((struct upnp_entry_lookup_t *) res)->entry_ptr;
  }

  if (ut->verbose) log_verbose ("Not Found\n");

  return NULL;
}

#ifdef _WIN32
static int
metadata_add_file (struct ushare_t *ut, struct upnp_entry_t *entry,
                   const char *file, const char *name, struct _stat64 *st_ptr)
#else
static int
metadata_add_file (struct ushare_t *ut, struct upnp_entry_t *entry,
                   const char *file, const char *name, struct  _stat64 *st_ptr)
#endif
{
  if (!entry || !file || !name)
    return -1;

#ifdef HAVE_DLNA
  if (ut->dlna_enabled || is_valid_extension (getExtension (file)))
#else
  if (is_valid_extension (getExtension (file)))
#endif
  {
    struct upnp_entry_t *child = NULL;

    child = upnp_entry_new (ut, name, file, entry, st_ptr->st_size, false);
    if (child)
      upnp_entry_add_child (ut, entry, child);

    return child->id;
  }

  return -1;
}

static void
metadata_add_container (struct ushare_t *ut,
                        struct upnp_entry_t *entry, const char *container)
{
  struct dirent **namelist = NULL;
  ssize_t n = 0,i = 0;

  if (!entry || !container)
    return;

#ifdef _WIN32
  n = scandir (container, &namelist, 0, NULL);
#else
  n = scandir (container, &namelist, 0, alphasort);
#endif
  if (n < 0)
  {
    perror ("scandir");
    return;
  }

  for (i = 0; i < n; i++)
  {
    struct _stat64 st;

    char *fullpath = NULL;

	if (NULL == &namelist)
	{
		break;
	}

	if (NULL == &namelist[i] || NULL == &namelist[i]->d_name[0])
	{
		continue;
	}

    if (namelist[i]->d_name[0] == '.')
    {
      free (namelist[i]);
      continue;
    }

    fullpath = (char *)
      malloc (strlen (container) + strlen (namelist[i]->d_name) + 2);
    sprintf (fullpath, "%s/%s", container, namelist[i]->d_name);

	if (ut->verbose)
		log_verbose ("%s\n", fullpath);


	{
#ifdef _WIN32
		wchar_t * wFilename = (wchar_t *) malloc((PATH_MAX+1)*sizeof(wchar_t*));
		_snwprintf(wFilename,PATH_MAX,L"%hs",fullpath);
		if (_wstat64 (wFilename, &st) < 0)
		{
			free (namelist[i]);
			free (fullpath);
			free (wFilename);
			continue;
		}
#else
		if ( _stat64 (fullpath, &st) < 0)
		{
			free (namelist[i]);
			free (fullpath);
			continue;
		}
#endif
  }

    if (S_ISDIR (st.st_mode))
    {
      struct upnp_entry_t *child = NULL;

      child = upnp_entry_new (ut, namelist[i]->d_name,
                              fullpath, entry, 0, true);
      if (child)
      {
        metadata_add_container (ut, child, fullpath);
        upnp_entry_add_child (ut, entry, child);
      }
    }
    else
	{
		if (S_ISREG (st.st_mode))
			metadata_add_file (ut, entry, fullpath, namelist[i]->d_name, &st);
	}

    free (namelist[i]);
    free (fullpath);
  }
  free (namelist);
}

void
free_metadata_list (struct ushare_t *ut)
{
  ut->init = 0;
  if (ut->root_entry)
    upnp_entry_free (ut, ut->root_entry);
  ut->root_entry = NULL;
  ut->nr_entries = 0;

  if (ut->rb)
  {
    rbdestroy (ut->rb);
    ut->rb = NULL;
  }

  ut->rb = rbinit (rb_compare, NULL);
  if (!ut->rb)
    log_error (_("Cannot create RB tree for lookups\n"));
}

void
build_metadata_list (struct ushare_t *ut)
{
  int i;
  log_info (_("Building Metadata List ...\n"));

  /* build root entry */
  if (!ut->root_entry)
    ut->root_entry = upnp_entry_new (ut, "root", NULL, NULL, -1, true);

  /* add files from content directory */
  for (i=0 ; i < ut->contentlist->count ; i++)
  {
    struct upnp_entry_t *entry = NULL;
    char *title = NULL;
    int size = 0;
	char * strContent = NULL;

	// trim the string
	{
		char const * const szContent = ut->contentlist->content[i];
		size_t lencontent = (strlen(szContent) +1) * sizeof(char *);
		char * strDirBuf = (char *) malloc(lencontent);
		size_t outlen = trimwhitespace(strDirBuf,lencontent,szContent);
		strContent = strDirBuf;
	}

	// replace \\ with /
	{
		size_t lencontent = (strlen(strContent) +1) * sizeof(char *);
		char * strDirBuf = (char *) malloc(lencontent);
		size_t p = 0;

		memset(strDirBuf,'\0',lencontent);

		for (; p < lencontent; p++)
		{
			if (strContent[p] == '\\')
				strDirBuf[p] = '/';
			else
				strDirBuf[p] = strContent[p];
		}
		free (strContent);
		strContent = strDirBuf;
	}

    log_info (_("Looking for files in content directory : %s\n"),
              strContent);

    size = strlen (strContent);
    if (strContent[size - 1] == '/') strContent[size - 1] = '\0';
    title = strrchr (strContent, '/');
    if (title)
      title++;
    else
    {
      /* directly use content directory name if no '/' before basename */
      title = ut->contentlist->content[i];
    }

    entry = upnp_entry_new (ut, title, strContent,
                            ut->root_entry, -1, true);

    if (!entry)
      continue;
    upnp_entry_add_child (ut, ut->root_entry, entry);
    metadata_add_container (ut, entry, strContent);
  }

  log_info (_("Found %d files and subdirectories.\n"), ut->nr_entries);
  ut->init = 1;
}

#ifdef _MSC_VER
int
rb_compare (const void *pa, const void *pb,
            const void *config)
#else
int
rb_compare (const void *pa, const void *pb,
            const void *config __attribute__ ((unused)))
#endif
{
  struct upnp_entry_lookup_t *a, *b;

  a = (struct upnp_entry_lookup_t *) pa;
  b = (struct upnp_entry_lookup_t *) pb;

  if (a->id < b->id)
    return -1;

  if (a->id > b->id)
    return 1;

  return 0;
}


