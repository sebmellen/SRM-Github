/* this file is part of srm http://srm.sourceforge.net/
   It is licensed under the MIT/X11 license */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "impl.h"

/**
 * print msg and its printf parameters to STDERR, prefixed by the program name.
 */
void error(char *msg, ...) {
  va_list ap;
  char buff[1024];

  va_start(ap, msg);
  vsnprintf(buff, sizeof(buff), msg, ap);
  fprintf(stderr, "%s: %s\n", program_name, buff);
  va_end(ap);
 }

/**
 * print msg and its printf parameters to STDERR, prefixed by the program name.
 * msg is followed by the description of errno.
 */
void errorp(char *msg, ...) {
  va_list ap;
  char buff[1024], buff2[1224];

  va_start(ap, msg);
  vsnprintf(buff, sizeof(buff), msg, ap);
  snprintf(buff2, sizeof(buff2), "%s: %s", program_name, buff);
  perror(buff2);
  va_end(ap);
}
