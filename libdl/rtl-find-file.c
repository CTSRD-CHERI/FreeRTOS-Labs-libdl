/*
 *  COPYRIGHT (c) 2012-2013 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtl
 *
 * @brief RTEMS Run-Time Linker Error
 */

#if HAVE_CONFIG_H
#include "waf_config.h"
#endif

#ifdef ipconfigUSE_FAT_LIBDL
#include "ff_headers.h"
#include "ff_stdio.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <rtl/rtl.h>
#include "rtl-find-file.h"
#include "rtl-error.h"
#include "rtl-string.h"
#include <rtl/rtl-trace.h>

#if WAF_BUILD
#define rtems_filesystem_is_delimiter rtems_filesystem_is_separator
#endif

bool
rtems_rtl_find_file (const char*  name,
                     const char*  paths,
                     const char** file_name,
                     size_t*      size)
{
#ifdef ipconfigUSE_FAT_LIBDL
  FF_Stat_t sb;
#else
  struct stat sb;
#endif

  *file_name = NULL;
  *size = 0;

  //if (rtems_filesystem_is_delimiter (name[0]) || (name[0] == '.'))

  // Always search the file system
  if (true)
  {
#ifdef ipconfigUSE_FAT_LIBDL
    if (ff_stat (name, &sb) == 0)
#else
    if (stat (name, &sb) == 0)
#endif
      *file_name = rtems_rtl_strdup (name);
  }
  else if (paths)
  {
    const char* start;
    const char* end;
    int         len;
    char*       fname;

    start = paths;
    end = start + strlen (paths);
    len = strlen (name);

    while (!*file_name && (start != end))
    {
      const char* delimiter = strchr (start, ':');

      if (delimiter == NULL)
        delimiter = end;

      /*
       * Allocate the path fragment, separator, name, terminating nul. Form the
       * path then see if the stat call works.
       */

      fname = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_OBJECT,
                                   (delimiter - start) + 1 + len + 1, true);
      if (!fname)
      {
        rtems_rtl_set_error (ENOMEM, "no memory searching for file");
        return false;
      }

      memcpy (fname, start, delimiter - start);
      fname[delimiter - start] = '/';
      memcpy (fname + (delimiter - start) + 1, name, len);

      if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
        printf ("rtl: find-file: path: %s\n", fname);

#ifdef ipconfigUSE_FAT_LIBDL
      if (ff_stat (name, &sb) == 0)
#else
      if (stat (fname, &sb) < 0)
#endif
        rtems_rtl_alloc_del (RTEMS_RTL_ALLOC_OBJECT, fname);
      else
        *file_name = fname;

      start = delimiter;
      if (start != end)
        ++start;
    }
  }

  if (!*file_name)
    return false;

  *size = sb.st_size;

  return true;
}
