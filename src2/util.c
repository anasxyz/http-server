#include <stdio.h>

#include "util.h"

void logs(char type, char *msg) {
  switch (type) {
  case 'E':
    fprintf(stderr, "ERROR: %s\n", msg);
    break;
  case 'W':
    fprintf(stderr, "WARNING: %s\n", msg);
    break;
  case 'I':
    fprintf(stderr, "INFO: %s\n", msg);
    break;
  case 'D':
    fprintf(stderr, "DEBUG: %s\n", msg);
    break;
  default:
    fprintf(stderr, "ERROR: Invalid log type: %c\n", type);
    break;
  }
}
