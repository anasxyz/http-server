#ifndef config_h
#define config_h

#include "route.h"

typedef struct {
  char* from;
  char* to;
} Alias;

extern int PORT;

extern char* ROOT;

extern Route* ROUTES;
extern size_t ROUTES_COUNT;

extern char **TRY_FILES;
extern size_t TRY_FILES_COUNT;

extern Alias *ALIASES;
extern size_t ALIASES_COUNT;

void load_mock_config();
void free_mock_config();

#endif /* config_h */
