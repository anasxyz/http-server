/*
 * NOT NEEDED FOR NOW
 *
 */

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

extern Route routes[];
extern const size_t num_routes;

Route* match_route(char *path);

#endif /* route_h */
