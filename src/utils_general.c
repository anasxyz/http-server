#include "../include/utils_general.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper: safe formatted string allocation (like asprintf)
char *strdup_printf(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int size = vsnprintf(NULL, 0, fmt, args) + 1;
  va_end(args);

  char *buf = malloc(size);
  if (!buf) return NULL;

  va_start(args, fmt);
  vsnprintf(buf, size, fmt, args);
  va_end(args);
  return buf;
}
