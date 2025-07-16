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
  char *method;
  char *path;
  HttpResponse* (*handler)();
} Route;

extern Route routes[];
extern const size_t num_routes;

HttpResponse* handle_status();

#endif /* route_h */
