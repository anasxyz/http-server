#ifndef route_h
#define route_h

#include "utils_http.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *prefix; // /api/
  char *host; // 127.0.0.1
  int port; // 5050
  char* backend_path; // /page/
} Route;

Route* match_route(char *path);
bool match_prefix(const char *path, const char *prefix);
char *trim_prefix(const char *path, const char *prefix);

#endif /* route_h */
