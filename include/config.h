#ifndef config_h
#define config_h

#include "route.h"

extern int SERVER_PORT;
extern const char* BACKEND_HOST;
extern int BACKEND_PORT;

extern const char* WEB_ROOT;

extern Route *routes;
extern size_t num_routes;

void load_config(const char *filename);

#endif /* config_h */
