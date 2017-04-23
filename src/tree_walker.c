/* this file is part of srm http://srm.sourceforge.net/
   It is licensed under the MIT/X11 license */

#include "config.h"

#if defined(__linux__)
/* Three kludges for the price of one; glibc's fts package doesn't
   support LFS, so fall back to nftw. Do it here, before the headers
   define _BSD_SOURCE and set the prefer BSD behavior, preventing us
   from including XPG behavior with nftw later on. Check for linux and
   not glibc for the same reason. Can't include features.h without
   prefering bsd.

   Once glibc has a working fts, remove nftw completely along with
   this hack.
 */

#undef HAVE_FTS_OPEN
#define _GNU_SOURCE
#endif

#if defined(__linux__) && defined(HAVE_FTS_OPEN)
/* the fts function does not like 64bit-on-32bit, but we don't need it here anyway. */
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#define LARGE_FILES_ARE_ENABLED 1
#endif
#ifdef _LARGE_FILES
#undef _LARGE_FILES
#define LARGE_FILES_ARE_ENABLED 1
#endif
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_FTS_OPEN
#include <fts.h>
#endif

#include "srm.h"
#include "impl.h"

/**
 * show msg and arg to the user and wait for a reply.
 * @return true if the user said YES; false otherwise.
 */
static int prompt_user(const char *msg, const char *arg)
{
  char inbuf[8];
  printf(msg, arg); fflush(stdout);
  if(fgets(inbuf, sizeof(inbuf), stdin) == 0) return 0;
  return inbuf[0]=='y' || inbuf[0]=='Y';
}

/**
 * check file permissions for path and modify them so we can remove the file eventually.
 * @return true if permissions are find; false otherwise.
 */
static int check_perms(const char *path)
{
  int fd=-1;
#ifdef _MSC_VER
  struct __stat64 statbuf;
#else
  struct stat statbuf;
#endif

  if(!path) return 0;

#ifdef _MSC_VER
  if (_stat64(path, &statbuf) < 0)
#else
  if (lstat(path, &statbuf) < 0)
#endif
    {
      return 0;
    }

  if ( S_ISREG(statbuf.st_mode) && ((fd = open(path, O_WRONLY)) < 0) && (errno == EACCES) )
    {
      if ( chmod(path, S_IRUSR | S_IWUSR) < 0 )
	{
	  errorp("Unable to reset %s to writable (probably not owner) ... skipping", path);
	  return 0;
	}
    }

  if(fd>=0)
    close(fd);
  return 1;
}

/**
 * ask the user for confirmation to remove path.
 * @param options bitfield of SRM_OPT_* bits
 * @return true if the file should be removed; false otherwise.
 */
static int prompt_file(const char *path, const int options)
{
  int fd=-1, return_value=1;
#ifdef _MSC_VER
  struct __stat64 statbuf;
#else
  struct stat statbuf;
#endif

  if(!path) return 0;

  if (options & SRM_OPT_F)
    {
      if (options & SRM_OPT_V)
	error("removing %s", path);
      return check_perms(path);
    }

#ifdef _MSC_VER
  if (_stat64(path, &statbuf) < 0)
#else
  if (lstat(path, &statbuf) < 0)
#endif
    {
      errorp("could not stat %s", path);
      return 0;
    }

  if ( S_ISREG(statbuf.st_mode) && ((fd = open(path, O_WRONLY)) < 0) && (errno == EACCES) )
    {
      /* Not a symlink, not writable */
      return_value = prompt_user("Remove write protected file %s? (y/n) ", path);
      if(return_value == 1)
	return_value = check_perms(path);
    }
  else
    {
      /* Writable file or symlink */
      if (options & SRM_OPT_I)
	return_value = prompt_user("Remove %s? (y/n) ", path);
    }

  if ((options & SRM_OPT_V) && return_value)
    error("removing %s", path);

  if(fd >= 0)
    close(fd); /* close if open succeeded, or silently fail */

  return return_value;
}

/**
 * callback function for FTS/FTW.
 * @param flag FTS/FTW flag.
 * @param options bitfield of SRM_OPT_* bits
 * @return true if the file was removed; false otherwise.
 */
int process_file(char *path, const int flag, const int options)
{
  if(!path) return 0;

  while (path[strlen(path) - 1] == SRM_DIRSEP)
    path[strlen(path)- 1] = '\0';

  switch (flag) {
#ifdef FTS_D
  case FTS_D:
    /* ignore directories */
    return 1;
#endif

#ifdef FTS_DC
  case FTS_DC:
    error("cyclic directory entry %s", path);
    return 0;
#endif

#ifdef FTS_DNR
  case FTS_DNR:
    error("%s: permission denied", path);
    return 0;
#endif

#ifdef FTS_DOT
  case FTS_DOT:
    return 1;
#endif

#ifdef FTS_DP
  case FTS_DP:
    if (options & SRM_OPT_R) {
      if (! prompt_file(path, options)) {
	return 0;
      }
      if (rename_unlink(path) < 0) {
	errorp("unable to remove %s", path);
	return 0;
      }
    } else {
      error("%s is a directory", path);
      return 0;
    }
    return 1;
#endif

#ifdef FTS_ERR
  case FTS_ERR:
    error("fts error on %s", path);
    return 0;
#endif

#ifdef FTS_NS
  case FTS_NS:
#endif
#ifdef FTS_NSOK
  case FTS_NSOK:
#endif
    /* if we have 32bit system and file is >2GiB the fts functions can not stat them, so we just ignore the fts error. */
#ifndef LARGE_FILES_ARE_ENABLED
    /* Ignore nonexistant files with -f */
    if ( !(options & SRM_OPT_F) )
      {
	error("unable to stat %s", path);
	return 0;
      }
#endif
    /* no break here */

#ifdef FTS_DEFAULT
  case FTS_DEFAULT:
#endif
#ifdef FTS_F
  case FTS_F:
#endif
#ifdef FTS_SL
  case FTS_SL:
#endif
#ifdef FTS_SLNONE
  case FTS_SLNONE:
#endif
    if (! prompt_file(path, options)) {
      return 0;
    }
    if (sunlink(path, options) < 0) {
      if (errno == EMLINK) {
	if (options & SRM_OPT_V) {
	  error("%s has multiple links, this one has been unlinked but not overwritten", path);
	}
	return 1;
      }
      errorp("unable to remove %s", path);
      return 0;
    }
    return 1;

  default:
    error("unknown fts flag: %i", flag);
  }
  return 0;
}

#ifdef HAVE_FTS_OPEN

/**
 * @param options bitfield of SRM_OPT_* bits
 * @return 0 if all files/directories could be removed; > 0 otherwise.
 */
int tree_walker(char **trees, const int options)
{
  FTSENT *current_file=0;
  FTS *stream=0;
  int i = 0, opt = FTS_PHYSICAL | FTS_NOCHDIR, ret = 0;

  if(!trees) return +1;

  /* remove trailing slashes free trees */
  while (trees[i] != NULL) {
    while (trees[i][strlen(trees[i]) - 1] == SRM_DIRSEP)
      trees[i][strlen(trees[i]) -1] = '\0';
    i++;
  }

  if(options & SRM_OPT_X)
    opt |= FTS_XDEV;

  if ( (stream = fts_open(trees, opt, NULL)) == NULL ) {
    errorp("fts_open() returned NULL");
    return +2;
  } else {
    while ( (current_file = fts_read(stream)) != NULL) {
      if (! process_file(current_file->fts_path, current_file->fts_info, options)) {
	ret = 1;
      }
      if ( !(options & SRM_OPT_R) )
	fts_set(stream, current_file, FTS_SKIP);
    }
    fts_close(stream);
  }
  return ret;
}

#elif HAVE_NFTW

#if defined(__digital__) && defined(__unix__)
/* Shut up tru64's cc(1) */
#define _XOPEN_SOURCE_EXTENDED
#endif

#define _GNU_SOURCE
#include <ftw.h>

static int ftw_options;
static int ftw_ret;

/**
 * @return true if walking the FTS tree should continue; false otherwise.
 */
static int ftw_process_path(const char *opath, const struct stat* statbuf, int flag, struct FTW* dummy)
{
  size_t path_size;
  char *path;
  int ret = 0;

  (void)statbuf;
  (void)dummy;

  if(!opath) return 0;

  path_size = strlen(opath) + 1;
  path = (char *)alloca(path_size);

  if (path == NULL) {
    errno = ENOMEM;
    return 0;
  }
  strncpy(path, opath, path_size);

  switch (flag) {
  case FTW_F:
    ret = process_file(path, FTS_F, ftw_options);
    break;
  case FTW_SL:
    ret = process_file(path, FTS_SL, ftw_options);
    break;
  case FTW_SLN:
    ret = process_file(path, FTS_SLNONE, ftw_options);
    break;
  case FTW_D:
    ret = process_file(path, FTS_D, ftw_options);
    break;
  case FTW_DP:
    ret = process_file(path, FTS_DP, ftw_options);
    break;
  case FTW_DNR:
    ret = process_file(path, FTS_DNR, ftw_options);
    break;
  case FTW_NS:
    ret = process_file(path, FTS_NS, ftw_options);
    break;
  default:
    error("unknown nftw flag: %i", flag);
  }
  if (! ret) {
    ftw_ret = +1;
  }
  return 0;
}

/**
 * @param options bitfield of SRM_OPT_* bits
 * @return 0 if all files/directories could be removed; > 0 otherwise.
 */
int tree_walker(char **trees, const int options)
{
  int i = 0;
  int opt = 0;

  if(!trees) return +2;
  ftw_options = options;
  ftw_ret = 0;

  if(ftw_options & SRM_OPT_X)
    opt |= FTW_MOUNT;
  if(ftw_options & SRM_OPT_R)
    opt |= FTW_DEPTH|FTW_PHYS;

  while (trees[i] != NULL)
    {
      /* remove trailing slashes */
      while (trees[i][strlen(trees[i]) - 1] == SRM_DIRSEP)
	trees[i][strlen(trees[i]) -1] = '\0';

      nftw(trees[i], ftw_process_path, 10, opt);
      ++i;
    }
  return ftw_ret;
}

#elif defined(_WIN32)
/* code is in win/tree.cpp */

#else
#error No tree traversal function found
#endif
