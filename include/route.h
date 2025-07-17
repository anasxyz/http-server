#ifndef route_h
#define route_h

#include "utils_http.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *prefix;
  char *host;
  int port;
} Route;

Route* match_route(char *path);
bool match_prefix(const char *path, const char *prefix);
char *trim_prefix(const char *path, const char *prefix);

#endif /* route_h */
