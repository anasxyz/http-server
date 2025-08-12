#include <stdarg.h>
#include <stdio.h>

#include "util.h"

int verbose_mode = 0;

void logs(char type, const char *fmt, const char *extra_fmt, ...) {
  va_list args;
  va_start(args, extra_fmt);

  // Copy the variadic arguments so we can use them twice
  va_list args_copy;
  va_copy(args_copy, args);

  // Print prefix
  switch (type) {
  case 'E':
    fprintf(stderr, "ERROR: ");
    break;
  case 'W':
    fprintf(stderr, "WARNING: ");
    break;
  case 'I':
    fprintf(stderr, "INFO: ");
    break;
  case 'D':
    fprintf(stderr, "DEBUG: ");
    break;
  default:
    fprintf(stderr, "ERROR: Invalid log type: %c\n", type);
    va_end(args);
    va_end(args_copy);
    return;
  }

  // Print main message
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  // Print extra if in verbose mode
  if (extra_fmt && verbose_mode) {
    fprintf(stderr, "REASON: ");
    vfprintf(stderr, extra_fmt, args_copy);
    fprintf(stderr, "\n");
  }

  va_end(args);
  va_end(args_copy);
}
