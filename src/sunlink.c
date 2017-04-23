/* this file is part of srm http://srm.sourceforge.net/
   It is licensed under the MIT/X11 license */

#include "config.h"

#ifdef _MSC_VER
#include "AltStreams.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__unix__)
#include <sys/ioctl.h>
#include <stdint.h>
#endif

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#if defined(__APPLE__)
#include <sys/disk.h>
#include <sys/paths.h>
#endif

#if defined(HAVE_SYS_PARAM_H) && defined(HAVE_SYS_MOUNT_H)
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if defined(HAVE_LINUX_EXT3_FS_H)
#include <linux/fs.h>
#include <linux/ext3_fs.h>

#define EXT2_IOC_GETFLAGS EXT3_IOC_GETFLAGS
#define EXT2_UNRM_FL EXT3_UNRM_FL
#define EXT2_IMMUTABLE_FL EXT3_IMMUTABLE_FL
#define EXT2_APPEND_FL EXT3_APPEND_FL
#define EXT2_IOC_SETFLAGS EXT3_IOC_SETFLAGS
#define EXT2_SECRM_FL EXT3_SECRM_FL
#define EXT2_IOC_SETFLAGS EXT3_IOC_SETFLAGS
#ifndef EXT2_SUPER_MAGIC
#define EXT2_SUPER_MAGIC EXT3_SUPER_MAGIC
#endif

#elif defined(HAVE_LINUX_EXT2_FS_H)
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#endif

#if defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#undef HAVE_SYS_XATTR_H
#undef HAVE_SYS_EXTATTR_H
#elif defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_SYS_EXTATTR_H
#elif defined(HAVE_SYS_EXTATTR_H)
#include <sys/extattr.h>
#include <libutil.h>
#endif

#include "srm.h"
#include "impl.h"

#ifndef O_SYNC
#define O_SYNC 0
#endif
#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#define KiB 1024
#define MiB (KiB*KiB)
#define GiB (KiB*KiB*KiB)

#ifdef _MSC_VER
typedef long long my_off_t;
#else
typedef off_t my_off_t;
#endif

struct srm_target
{
  int fd;
  const char* file_name;
  my_off_t file_size;
  unsigned char *buffer;
  unsigned buffer_size;
  int options;
};

static volatile int SIGINT_received = 0;
#if defined(__unix__)
#include <signal.h>
#if defined(__linux__) && !defined(__USE_GNU)
typedef __sighandler_t sighandler_t;
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__)
typedef sig_t sighandler_t;
#endif

static void sigint_handler(int signo)
{
  SIGINT_received = signo;
}
int sunlink_impl(const char *path, const int options);

int sunlink(const char *path, const int options)
{
#ifdef SIGUSR2
  sighandler_t usr2=signal(SIGUSR2, sigint_handler);
#endif
#ifdef SIGINFO
  sighandler_t info=signal(SIGINFO, sigint_handler);
#endif
#ifdef SIGPIPE
  sighandler_t pipe=signal(SIGPIPE, SIG_IGN);
#endif

  int ret=sunlink_impl(path, options);

#ifdef SIGPIPE
  signal(SIGPIPE, pipe);
#endif
#ifdef SIGINFO
  signal(SIGINFO, info);
#endif
#ifdef SIGUSR2
  signal(SIGUSR2, usr2);
#endif
  return ret;
}

#else /* __unix__ */
#define sunlink_impl sunlink
#endif

/**
   writes a buffer to a file descriptor. Ensures that the complete
   buffer is written.

   ripped from Advanced Programming in the Unix Environment by Richard Stevens

   @param fd file descriptor
   @param buf pointer to a buffer
   @param count size of buf in bytes

   @return upon success the number of bytes written, upon error the negative return code from write() (see the errno variable for details)
*/
static ssize_t writen(const int fd, const void* buf, const size_t count)
{
  const char *ptr=(const char*)buf;
  size_t nleft=count;

  if(fd<0 || !buf) return -1;

  while(nleft > 0)
    {
      ssize_t nwritten;
      if( (nwritten=write(fd, ptr, nleft)) < 0)
	return nwritten;
      nleft -= nwritten;
      ptr   += nwritten;
    }

  return count;
}

static void flush(int fd)
{
  /* force buffered writes to be flushed to disk */
#if defined F_FULLFSYNC
  /* F_FULLFSYNC is equivalent to fsync plus device flush to media */
  if (fcntl(fd, F_FULLFSYNC, NULL) != 0) {
    /* we're not on a fs that supports this; fall back to plain fsync */
    fsync(fd);
  }
#elif HAVE_FDATASYNC
  fdatasync(fd);
#else
  fsync(fd);
#endif
}

#if defined(HAVE_ATTR_XATTR_H) || defined(HAVE_SYS_XATTR_H) || defined(HAVE_SYS_EXTATTR_H)
static int extattr_overwrite(struct srm_target *srm, const int pass, const int attrnamespace)
{
  char *list = NULL;
  unsigned char *value = NULL;
  size_t list_size = 256, value_size = 0, key_len = 0;
  ssize_t len = 0, i = 0;
  /* get list of atrributes */
  for(;;) {
    list = alloca(list_size);
    if (! list) {
      errno = ENOMEM;
      return -1;
    }
    errno = 0;
#if defined(HAVE_ATTR_XATTR_H) || defined(HAVE_SYS_XATTR_H)
#if defined(HAVE_ATTR_XATTR_H)
    len = flistxattr(srm->fd, list, list_size);
#elif defined(HAVE_SYS_XATTR_H) && defined(__APPLE__)
    len = flistxattr(srm->fd, list, list_size, 0);
#endif
    if (len < 0 && errno == ERANGE) {
      list_size *= 2;
      if (list_size > 1024*1024) {
	error("file has very large extended attribute list, giving up.");
	break;
      }
      continue;
    }
#elif defined(HAVE_SYS_EXTATTR_H)
    len = extattr_list_fd(srm->fd, attrnamespace, NULL, 0);
    if (len > list_size) {
      list_size = len;
      continue;
    }
    len = extattr_list_fd(srm->fd, attrnamespace, list, list_size);
#endif
    if (len < 0) {
      break;
    }
    break;
  }

  /* iterate list of attributes */
  for(i = 0; i < len; i += key_len + 1) {
    int ret = 0;
    ssize_t val_len = 0;
    char *key = NULL;
#if defined(HAVE_ATTR_XATTR_H) || defined(HAVE_SYS_XATTR_H)
    key = list + i;
    key_len = strlen(key);
#if defined(HAVE_ATTR_XATTR_H)
    val_len = fgetxattr(srm->fd, key, NULL, 0);
#elif defined(HAVE_SYS_XATTR_H) && defined(__APPLE__)
    val_len = fgetxattr(srm->fd, key, NULL, 0, 0, 0);
#endif
#elif defined(HAVE_SYS_EXTATTR_H)
    char keybuf[257];
    key_len = *((unsigned char*)(list + i));
    memcpy(keybuf, list+i+1, key_len);
    keybuf[key_len] = 0;
    key = keybuf;
    val_len = extattr_get_fd(srm->fd, attrnamespace, key, NULL, 0);
#endif
    if ( ((srm->options & SRM_OPT_V) > 1) && pass == 1) {
      char *name_space="";
#if defined(HAVE_SYS_EXTATTR_H)
      extattr_namespace_to_string(attrnamespace, &namespace);
#endif
      error("found extended attribute %s %s of %i bytes", name_space, key, (int)val_len);
    }
    if (val_len > (ssize_t)value_size) {
      value_size = val_len;
      value = alloca(value_size);
      if (! value) {
	errno = ENOMEM;
	return -1;
      }
      fill(value, value_size, srm->buffer, srm->buffer_size);
    }
#if defined(HAVE_ATTR_XATTR_H)
    ret = fsetxattr(srm->fd, key, value, val_len, XATTR_REPLACE);
#elif defined(HAVE_SYS_XATTR_H) && defined(__APPLE__)
    ret = fsetxattr(srm->fd, key, value, val_len, 0, XATTR_REPLACE);
#elif defined(HAVE_SYS_EXTATTR_H)
    ret = extattr_set_fd(srm->fd, attrnamespace, key, value, val_len);
#endif
    if (ret < 0) {
      errorp("could not overwrite extended attribute %s", key);
    }
  }

  (void)attrnamespace;
  return 0;
}
#endif

static int overwrite(struct srm_target *srm, const int pass)
{
  unsigned last_val = ~0u;
  my_off_t i = 0;
  ssize_t w;

  if(!srm) return -1;
  if(!srm->buffer) return -1;
  if(srm->buffer_size < 1) return -1;

  /* check for extended attributes */
#if defined(HAVE_ATTR_XATTR_H) || defined(HAVE_SYS_XATTR_H)
  if (extattr_overwrite(srm, pass, 0) < 0) {
    return -1;
  }
#elif defined(HAVE_SYS_EXTATTR_H)
  if (extattr_overwrite(srm, pass, EXTATTR_NAMESPACE_USER) < 0) {
    return -1;
  }
  if (extattr_overwrite(srm, pass, EXTATTR_NAMESPACE_SYSTEM) < 0) {
    return -1;
  }
#endif

  if(lseek(srm->fd, 0, SEEK_SET) != 0)
    {
      perror("could not seek");
      return -1;
    }

  if(srm->file_size < (my_off_t)(srm->buffer_size))
    {
      w=writen(srm->fd, srm->buffer, srm->file_size);
      if(w != srm->file_size)
	return -1;
    }
  else
    {
      while (i < srm->file_size - (my_off_t)srm->buffer_size)
	{
	  w=writen(srm->fd, srm->buffer, srm->buffer_size);
	  if(w != (ssize_t)(srm->buffer_size))
	    return -1;
	  i += w;

	  if ((srm->options & SRM_OPT_V) > 1 || SIGINT_received) {
	      unsigned val = 0, file_size = 0;
	      char c = '.';
	      if (srm->file_size < MiB) {
		  val = i / KiB;
		  file_size = (unsigned)(srm->file_size/KiB);
		  c = 'K';
	      } else if(srm->file_size < GiB) {
		  val = i / MiB;
		  file_size = (unsigned)(srm->file_size/MiB);
		  c = 'M';
	      } else {
		  val = i / GiB;
		  file_size = (unsigned)(srm->file_size/GiB);
		  c = 'G';
	      }
	      if (val != last_val) {
		  printf("\rpass %i %u%ciB/%u%ciB     ", pass, val, c, file_size, c);
		  fflush(stdout);
		  last_val = val;
	      }

	      if(SIGINT_received)
		{
		  if(srm->file_name)
		    printf("%s\n", srm->file_name);
		  else
		    putchar('\n');
		  SIGINT_received=0;
		  fflush(stdout);
		}
	    }
	}
      w=writen(srm->fd, srm->buffer, srm->file_size - i);
      if(w != srm->file_size-i)
	return -1;
    }

  if((srm->options & SRM_OPT_V) > 1)
    {
      printf("\rpass %i sync                        ", pass);
      fflush(stdout);
    }

  flush(srm->fd);

  if(lseek(srm->fd, 0, SEEK_SET) != 0)
    {
      perror("could not seek");
      return -1;
    }

  return 0;
}

static int overwrite_random(struct srm_target *srm, const int pass, const int num_passes)
{
  int i;

  if(!srm) return -1;
  if(!srm->buffer) return -1;
  if(srm->buffer_size < 1) return -1;

  for (i = 0; i < num_passes; i++)
    {
      randomize_buffer(srm->buffer, srm->buffer_size);
      if(overwrite(srm, pass+i) < 0)
	return -1;
    }

  return 0;
}

static int overwrite_byte(struct srm_target *srm, const int pass, const int byte)
{
  if(!srm) return -1;
  if(!srm->buffer) return -1;
  if(srm->buffer_size < 1) return -1;
  memset(srm->buffer, byte, srm->buffer_size);
  return overwrite(srm, pass);
}

static int overwrite_bytes(struct srm_target *srm, const int pass, const unsigned char byte1, const unsigned char byte2, const unsigned char byte3)
{
  unsigned char buf[3];

  if(!srm) return -1;
  if(!srm->buffer) return -1;
  if(srm->buffer_size < 1) return -1;

  buf[0] = byte1;
  buf[1] = byte2;
  buf[2] = byte3;
  fill(srm->buffer, srm->buffer_size, buf, sizeof(buf));
  return overwrite(srm, pass);
}

static int overwrite_string(struct srm_target *srm, const int pass, const char *str)
{
  if(!srm) return -1;
  if(!srm->buffer) return -1;
  if(srm->buffer_size < 1) return -1;
  if (!str) return -1;

  fill(srm->buffer, srm->buffer_size, (const unsigned char*)str, strlen(str));
  return overwrite(srm, pass);
}

static int overwrite_selector(struct srm_target *srm)
{
  if(!srm) return -1;

#if defined(F_NOCACHE)
  /* before performing file I/O, set F_NOCACHE to prevent caching */
  (void)fcntl(srm->fd, F_NOCACHE, 1);
#endif

  if( (srm->buffer = (unsigned char *)alloca(srm->buffer_size)) == NULL )
    {
      errno = ENOMEM;
      return -1;
    }

  if(srm->options & SRM_MODE_DOD)
    {
      if((srm->options&SRM_OPT_V) > 1)
	error("US DoD mode");
      if(overwrite_byte(srm, 1, 0xF6) < 0) return -1;
      if(overwrite_byte(srm, 2, 0x00) < 0) return -1;
      if(overwrite_byte(srm, 3, 0xFF) < 0) return -1;
      if(overwrite_random(srm, 4, 1) < 0) return -1;
      if(overwrite_byte(srm, 5, 0x00) < 0) return -1;
      if(overwrite_byte(srm, 6, 0xFF) < 0) return -1;
      if(overwrite_random(srm, 7, 1) < 0) return -1;
    }
  else if(srm->options & SRM_MODE_DOE)
    {
      if((srm->options&SRM_OPT_V) > 1)
	error("US DoE mode");
      if(overwrite_random(srm, 1, 2) < 0) return -1;
      if(overwrite_bytes(srm, 3, 'D', 'o', 'E') < 0) return -1;
    }
  else if(srm->options & SRM_MODE_OPENBSD)
    {
      if((srm->options&SRM_OPT_V) > 1)
	error("OpenBSD mode");
      if(overwrite_byte(srm, 1, 0xFF) < 0) return -1;
      if(overwrite_byte(srm, 2, 0x00) < 0) return -1;
      if(overwrite_byte(srm, 3, 0xFF) < 0) return -1;
    }
  else if(srm->options & SRM_MODE_SIMPLE)
    {
      if((srm->options&SRM_OPT_V) > 1)
	error("Simple mode");
      if(overwrite_byte(srm, 1, 0x00) < 0) return -1;
    }
  else if(srm->options & SRM_MODE_RCMP)
    {
      if((srm->options&SRM_OPT_V) > 1)
	error("RCMP mode");
      if(overwrite_byte(srm, 1, 0x00) < 0) return -1;
      if(overwrite_byte(srm, 2, 0xFF) < 0) return -1;
      if(overwrite_string(srm, 3, "RCMP") < 0) return -1;
    }
  else
    {
      if(! (srm->options & SRM_MODE_35))
	error("something is strange, did not have mode_35 bit");
      if((srm->options&SRM_OPT_V) > 1)
	error("Full 35-pass mode (Gutmann method)");
      if(overwrite_random(srm, 1, 4) < 0) return -1;
      if(overwrite_byte(srm, 5, 0x55) < 0) return -1;
      if(overwrite_byte(srm, 6, 0xAA) < 0) return -1;
      if(overwrite_bytes(srm, 7, 0x92, 0x49, 0x24) < 0) return -1;
      if(overwrite_bytes(srm, 8, 0x49, 0x24, 0x92) < 0) return -1;
      if(overwrite_bytes(srm, 9, 0x24, 0x92, 0x49) < 0) return -1;
      if(overwrite_byte(srm, 10, 0x00) < 0) return -1;
      if(overwrite_byte(srm, 11, 0x11) < 0) return -1;
      if(overwrite_byte(srm, 12, 0x22) < 0) return -1;
      if(overwrite_byte(srm, 13, 0x33) < 0) return -1;
      if(overwrite_byte(srm, 14, 0x44) < 0) return -1;
      if(overwrite_byte(srm, 15, 0x55) < 0) return -1;
      if(overwrite_byte(srm, 16, 0x66) < 0) return -1;
      if(overwrite_byte(srm, 17, 0x77) < 0) return -1;
      if(overwrite_byte(srm, 18, 0x88) < 0) return -1;
      if(overwrite_byte(srm, 19, 0x99) < 0) return -1;
      if(overwrite_byte(srm, 20, 0xAA) < 0) return -1;
      if(overwrite_byte(srm, 21, 0xBB) < 0) return -1;
      if(overwrite_byte(srm, 22, 0xCC) < 0) return -1;
      if(overwrite_byte(srm, 23, 0xDD) < 0) return -1;
      if(overwrite_byte(srm, 24, 0xEE) < 0) return -1;
      if(overwrite_byte(srm, 25, 0xFF) < 0) return -1;
      if(overwrite_bytes(srm, 26, 0x92, 0x49, 0x24) < 0) return -1;
      if(overwrite_bytes(srm, 27, 0x49, 0x24, 0x92) < 0) return -1;
      if(overwrite_bytes(srm, 28, 0x24, 0x92, 0x49) < 0) return -1;
      if(overwrite_bytes(srm, 29, 0x6D, 0xB6, 0xDB) < 0) return -1;
      if(overwrite_bytes(srm, 30, 0xB6, 0xDB, 0x6D) < 0) return -1;
      if(overwrite_bytes(srm, 31, 0xDB, 0x6D, 0xB6) < 0) return -1;
      if(overwrite_random(srm, 32, 4) < 0) return -1;
      /* if you want to backup your partition or shrink your vmware image having the file zero-ed gives best compression results. */
      if(overwrite_byte(srm, 36, 0x00) < 0) return -1;
    }
#if 0
  if((srm->options & SRM_OPT_V) > 1)
    printf("\n");
#endif
  return 0;
}

#ifdef _MSC_VER
static my_off_t getFileSize(WCHAR *fn)
{
    /* it's a pain, but it seems that the only way to get the size of an alternate data stream is to read it once. */
#if 1
    my_off_t size = 0, r;
    int fd = _wopen(fn, O_RDONLY);
    if (fd < 0) return 0;
    do {
        char buf[4096];
	r = read(fd, buf, sizeof(buf));
	if (r > 0) size += r;
    } while(r > 0);
    close(fd);
    return size;
#else
    WCHAR fn2[32767];
    WIN32_FILE_ATTRIBUTE_DATA w32_fad;

    wcscpy(fn2, L"\\\\?\\");
    wcscat(fn2, fn);
    if (! GetFileAttributesEx((char*)fn2, GetFileExInfoStandard, &w32_fad)) {
	error("could not get file attributes of %S", fn2);
	return 0;
    }
    return (((long long)w32_fad.nFileSizeHigh) << 32) | ((long long)w32_fad.nFileSizeLow);
#endif
}

int ntfsHardLinks(const char *fn)
{
    int ret = -1;
    BY_HANDLE_FILE_INFORMATION hfi;
    HANDLE hFile = CreateFile(fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
	return ret;
    }

    if (GetFileInformationByHandle(hFile, &hfi)) {
	ret = hfi.nNumberOfLinks;
    }

    CloseHandle(hFile);
    return ret;
}
#endif

int sunlink_impl(const char *path, const int options)
{
  const int oflags = O_WRONLY|O_SYNC|_O_BINARY;
  struct srm_target srm;
#if defined(_MSC_VER)
  struct __stat64 statbuf;
#else
  struct stat statbuf;
#endif
#if defined(__unix__) || defined(__APPLE__)
  struct flock flock;
#endif

  /* check function arguments */
  if(!path) return -1;

  memset(&srm, 0, sizeof(srm));
  srm.file_name = path;
  srm.options = options;

  /* check if path exists */
#if defined(_MSC_VER)
  if (_stat64(path, &statbuf) < 0) {
    return -1;
  }
#else
  if (lstat(path, &statbuf) < 0) {
    return -1;
  }
#endif

  srm.file_size = statbuf.st_size;
  if (srm.file_size < 0) {
    error("%s : file size: %lli, can not work with negative values", path, (long long)srm.file_size);
    return -1;
  }
#ifdef _MSC_VER
  srm.buffer_size = 4096;
#else
  srm.buffer_size = statbuf.st_blksize;
#endif
  if(srm.buffer_size < 16)
    srm.buffer_size = 512;
  if((srm.options & SRM_OPT_V) > 2)
    error("file size: %lli, buffer_size=%u", (long long)srm.file_size, srm.buffer_size);

#if defined(__linux__)
  if(S_ISBLK(statbuf.st_mode))
    {
      int secsize=512;
      long blocks=0;
      uint64_t u=0, u_;

      if( (srm.fd = open(srm.file_name, O_WRONLY)) < 0)
	return -1;

      if(ioctl(srm.fd, BLKSSZGET, &secsize) < 0)
	{
	  perror("could not ioctl(BLKSSZGET)");
	  return -1;
	}
      if((options&SRM_OPT_V) > 2)
	error("sector size %i bytes", secsize);

      if(ioctl(srm.fd, BLKGETSIZE, &blocks) < 0)
	{
	  perror("could not ioctl(BLKGETSIZE)");
	  return -1;
	}
      if((options&SRM_OPT_V) > 2)
	error("BLKGETSIZE %i blocks", (int)blocks);

      if(ioctl(srm.fd, BLKGETSIZE64, &u) < 0)
	{
	  perror("could not ioctl(BLKGETSIZE64)");
	  return -1;
	}
      if((options&SRM_OPT_V) > 2)
	error("BLKGETSIZE64 %llu bytes", (unsigned long long)u);

      u_=((uint64_t)blocks)*secsize;
      if(u_ != u)
	  error("!Warning! sectorsize*blocks:%llu != bytes:%llu", (long long unsigned) u_, (long long unsigned) u);

      srm.file_size = u;
      srm.buffer_size = secsize;

      if(srm.file_size == 0)
	{
	  close(srm.fd);
	  if (srm.options & SRM_OPT_V)
	    error("could not determine block device %s filesize", srm.file_name);
	  errno = EIO;
	  return -1;
	}

      if((options&SRM_OPT_V) > 1)
	error("block device %s size: %llu bytes", srm.file_name, (unsigned long long)u);

      if(overwrite_selector(&srm) < 0)
	{
	  int e=errno;
	  if (srm.options & SRM_OPT_V)
	    errorp("could not overwrite device %s", srm.file_name);
	  close(srm.fd);
	  errno=e;
	  return -1;
	}
      close(srm.fd);
      return 0;
    }
#endif

    if (!S_ISREG(statbuf.st_mode)) {
	return rename_unlink(srm.file_name);
    }

#if defined(_MSC_VER)
  /* check for alternate NTFS data streams */
  /* code taken from http://www.flexhex.com/docs/articles/alternate-streams.phtml */
  {
    static NTQUERYINFORMATIONFILE NtQueryInformationFile = 0;
    char InfoBlock[64 * 1024];
    PFILE_STREAM_INFORMATION pStreamInfo = (PFILE_STREAM_INFORMATION)InfoBlock;

    if (! NtQueryInformationFile) {
	NtQueryInformationFile = (NTQUERYINFORMATIONFILE) GetProcAddress(GetModuleHandle("ntdll.dll"), "NtQueryInformationFile");
    }

    /* get information on current file */
    if (NtQueryInformationFile) {
	IO_STATUS_BLOCK ioStatus;
	HANDLE hFile = CreateFile(srm.file_name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	NtQueryInformationFile(hFile, &ioStatus, InfoBlock, sizeof(InfoBlock), FileStreamInformation);
	CloseHandle(hFile);

    /* iterate over information looking from streams */
    for (;;) {
	WCHAR wszStreamName[MAX_PATH];
	int i;
	char ads_fn[MAX_PATH], buf[MAX_PATH];
	WCHAR ads_fn_w[MAX_PATH];
	struct srm_target ads = srm;

	/* Check if stream info block is empty (directory may have no stream) */
	if (pStreamInfo->StreamNameLength == 0) break;

	/* Get null-terminated stream name */
	memcpy(wszStreamName, pStreamInfo->StreamName, pStreamInfo->StreamNameLength);
	wszStreamName[pStreamInfo->StreamNameLength / sizeof(WCHAR)] = L'\0';

	/* skip the default data stream */
	if (wcscmp(wszStreamName, L"::$DATA") == 0) {
	    goto next_ads;
	}

	i = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wszStreamName, -1, buf, sizeof(buf)-1, NULL, NULL);
	if (i > 0) {
	    buf[i] = 0;
	    snprintf(ads_fn, sizeof(ads_fn), "%s%s", srm.file_name, buf);
	    ads.file_name = ads_fn;
	}

	i = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, srm.file_name, -1, ads_fn_w, MAX_PATH);
	if (i <= 0) {
	    error("could not convert %S to multi byte string", wszStreamName);
	    goto next_ads;
	}
	ads_fn_w[i] = 0;
	wcscat(ads_fn_w, wszStreamName);

	ads.file_size = getFileSize(ads_fn_w);
	if (ads.file_size == 0) {
	    if ((srm.options & SRM_OPT_V) > 1) {
		error("skipping alternate data stream %S of %lli bytes %i %i", ads_fn_w, ads.file_size);
	    }
	    goto next_ads;
	}

	ads.fd = _wopen(ads_fn_w, oflags);
	if (ads.fd < 0) {
	    errorp("could not open alternate data stream %S", wszStreamName);
	    goto next_ads;
	}

	if (srm.options & SRM_OPT_V) {
	    error("removing alternate data stream %S of %lli bytes", ads_fn_w, ads.file_size);
	}
	if (ads.file_size > 0) {
	    if(overwrite_selector(&ads) < 0)
		{
		if (ads.options & SRM_OPT_V)
		    errorp("could not overwrite alternate data stream %S", ads_fn_w);
		}
	    ftruncate(ads.fd, 0);
	}
	close(ads.fd);

next_ads:
	if (pStreamInfo->NextEntryOffset == 0) break;
	pStreamInfo = (PFILE_STREAM_INFORMATION) ((LPBYTE)pStreamInfo + pStreamInfo->NextEntryOffset);
    }
    }
  }
#endif

#ifdef _MSC_VER
  if (ntfsHardLinks(srm.file_name) > 1)
#else
  if (statbuf.st_nlink > 1)
#endif
  {
    rename_unlink(srm.file_name);
    errno = EMLINK;
    return -1;
  }

  if (srm.file_size==0) {
    return rename_unlink(srm.file_name);
  }

  if ( (srm.fd = open(srm.file_name, oflags)) < 0)
    return -1;

#if defined(__unix__) || defined(__APPLE__)
  flock.l_type = F_WRLCK;
  flock.l_whence = SEEK_SET;
  flock.l_start = 0;
  flock.l_len = 0;
  if (fcntl(srm.fd, F_SETLK, &flock) < 0) {
    int e=errno;
    flock.l_type = F_WRLCK;
    flock.l_whence = SEEK_SET;
    flock.l_start = 0;
    flock.l_len = 0;
    flock.l_pid = 0;
    if (fcntl(srm.fd, F_GETLK, &flock) == 0 && flock.l_pid > 0) {
      error("can't unlink %s, locked by process %i", srm.file_name, flock.l_pid);
    }
    close(srm.fd);
    errno=e;
    return -1;
  }
#endif

#if defined(HAVE_SYS_VFS_H) || (defined(HAVE_SYS_PARAM_H) && defined(HAVE_SYS_MOUNT_H))
  {
    struct statfs fs_stats;
    if (fstatfs(srm.fd, &fs_stats) < 0 && errno != ENOSYS)
      {
	int e=errno;
	close(srm.fd);
	errno=e;
	return -1;
      }

#if defined(__linux__)
    srm.buffer_size = fs_stats.f_bsize;
#elif defined(__FreeBSD__) || defined(__APPLE__)
    srm.buffer_size = fs_stats.f_iosize;
#else
#error Please define your platform.
#endif
    if((srm.options & SRM_OPT_V) > 2)
      error("buffer_size=%u", srm.buffer_size);

#if defined(HAVE_LINUX_EXT2_FS_H) || defined(HAVE_LINUX_EXT3_FS_H)
    if (fs_stats.f_type == EXT2_SUPER_MAGIC ) /* EXT2_SUPER_MAGIC and EXT3_SUPER_MAGIC are the same */
      {
	int flags = 0;

	  if (ioctl(srm.fd, EXT2_IOC_GETFLAGS, &flags) < 0)
	    {
	      int e=errno;
	      close(srm.fd);
	      errno=e;
	      return -1;
	    }

	  if ( (flags & EXT2_UNRM_FL) ||
	       (flags & EXT2_IMMUTABLE_FL) ||
	       (flags & EXT2_APPEND_FL) )
	    {
              if((srm.options & SRM_OPT_V) > 2) {
                  error("%s has ext2 undelete, immutable or append-only flag", srm.file_name);
              }
	      close(srm.fd);
	      errno = EPERM;
	      return -1;
	    }

#ifdef HAVE_LINUX_EXT3_FS_H
	  /* if we have the required capabilities we can disable data journaling on ext3 */
	  if(fs_stats.f_type == EXT3_SUPER_MAGIC) /* superflous check again, just to make it clear again */
	    {
	      flags &= ~EXT3_JOURNAL_DATA_FL;
	      if (ioctl(srm.fd, EXT3_IOC_SETFLAGS, flags) < 0) {
                  if (srm.options & SRM_OPT_V)
                      errorp("could not clear journal data flag for ext3 on %s", srm.file_name);
	      }
	    }
#endif
	}
#endif /* HAVE_LINUX_EXT2_FS_H */
  }
#endif /* HAVE_SYS_VFS_H */

/* chflags(2) turns out to be a different system call in every BSD
   derivative. The important thing is to make sure we'll be able to
   unlink it after we're through messing around. Unlinking it first
   would remove the need for any of these checks, but would leave the
   user with no way to overwrite the file if the process was
   interupted during the overwriting. So, instead we assume that the
   open() above will fail on immutable and append-only files and try
   and catch only platforms supporting NOUNLINK here.

   OpenBSD - doesn't support nounlink (As of 3.1)
   FreeBSD - supports nounlink (from 4.4 on?)
   Tru64   - unknown
   MacOS X - doesn't support NOUNLINK (as of 10.3.5)
*/

#if defined(HAVE_CHFLAGS) && defined(__FreeBSD__)
  if ((statbuf.st_flags & UF_IMMUTABLE) ||
      (statbuf.st_flags & UF_APPEND) ||
      (statbuf.st_flags & UF_NOUNLINK) ||
      (statbuf.st_flags & SF_IMMUTABLE) ||
      (statbuf.st_flags & SF_APPEND) ||
      (statbuf.st_flags & SF_NOUNLINK))
    {
      if((srm.options & SRM_OPT_V) > 2) {
          error("%s has ext2 nounlink, immutable or append-only flag", srm.file_name);
      }
      close(srm.fd);
      errno = EPERM;
      return -1;
    }
#endif /* HAVE_CHFLAGS */

  /* check that the srm struct contains useful values */
  if (srm.file_name == 0) {
    error("internal error: srm.file_name is NULL");
    close(srm.fd);
    errno = ENOSYS;
    return -1;
  }
  if (srm.file_size == 0) {
    error("internal error: srm.file_size is 0");
    close(srm.fd);
    errno = ENOSYS;
    return -1;
  }
  if (srm.buffer_size == 0) {
    error("internal error: srm.buffer_size is 0");
    close(srm.fd);
    errno = ENOSYS;
    return -1;
  }

  if(overwrite_selector(&srm) < 0)
    {
      int e=errno;
      if (srm.options & SRM_OPT_V)
	errorp("could not overwrite file %s", srm.file_name);
      close(srm.fd);
      errno=e;
      return -1;
    }

#if defined(HAVE_LINUX_EXT2_FS_H) || defined(HAVE_LINUX_EXT3_FS_H)
  ioctl(srm.fd, EXT2_IOC_SETFLAGS, EXT2_SECRM_FL);
#endif

  if (ftruncate(srm.fd, 0) < 0) {
    int e=errno;
    close(srm.fd);
    errno=e;
    return -1;
  }

  close(srm.fd);
  srm.fd = -1;

#ifdef __APPLE__
  /* Also overwrite the file's resource fork, if present. */
  {
    struct srm_target rsrc = srm;
    rsrc.file_name = (char *)alloca(strlen(srm.file_name) + sizeof(_PATH_RSRCFORKSPEC) + 1);
    if (rsrc.file_name == NULL)
      {
	errno = ENOMEM;
	goto rsrc_fork_failed;
      }

    if (snprintf((char*)rsrc.file_name, MAXPATHLEN, "%s" _PATH_RSRCFORKSPEC, srm.file_name) > MAXPATHLEN - 1)
      {
	errno = ENAMETOOLONG;
	goto rsrc_fork_failed;
      }

    if (lstat(rsrc.file_name, &statbuf) != 0)
      {
	if (errno == ENOENT || errno == ENOTDIR) {
	  rsrc.file_size = 0;
	} else {
	  goto rsrc_fork_failed;
	}
      }
    else
      {
	rsrc.file_size = statbuf.st_size;
      }

    if (rsrc.file_size > 0)
      {
	if ((rsrc.fd = open(rsrc.file_name, oflags)) < 0) {
	  goto rsrc_fork_failed;
	}

	flock.l_type = F_WRLCK;
	flock.l_whence = SEEK_SET;
	flock.l_start = 0;
	flock.l_len = 0;
	if (fcntl(rsrc.fd, F_SETLK, &flock) == -1)
	  {
	    close(rsrc.fd);
	    goto rsrc_fork_failed;
	  }

	if (rsrc.options & SRM_OPT_V) {
	  error("removing %s", rsrc.file_name);
	}

	if(overwrite_selector(&rsrc) < 0)
	  {
	    if (rsrc.options & SRM_OPT_V) {
	      errorp("could not overwrite resource fork %s", rsrc.file_name);
	    }
	  }

	ftruncate(rsrc.fd, 0);
	close(rsrc.fd);
      }
    goto rsrc_fork_done;

  rsrc_fork_failed:
    if (rsrc.options & SRM_OPT_V) {
      errorp("could not access resource fork %s", srm.file_name);
    }

  rsrc_fork_done: ;
  }
#endif /* __APPLE__ */

  return rename_unlink(srm.file_name);
}
