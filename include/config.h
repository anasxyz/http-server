#ifndef config_h
#define config_h

#include "route.h"

extern int SERVER_PORT;
extern char* WEB_ROOT;
extern Route *routes;
extern size_t num_routes;

void load_config(const char *filename);
void free_config();

#endif /* config_h */
