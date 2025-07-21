#ifndef config_h
#define config_h

#include "route.h"

typedef struct {
  char* from; // request prefix like "/images/"
  char* to; // file path like "/var/www/stuff/images/"
} Alias;

typedef struct {
  char *from;  // request prefix like "/api/"
  char *to;    // backend base URL like "http://localhost:5000/"
} Proxy;

extern int PORT;
extern int DEFAULT_PROXY_PORT;
extern char* ROOT;

extern Route* ROUTES;
extern size_t ROUTES_COUNT;

extern char **TRY_FILES;
extern size_t TRY_FILES_COUNT;

extern Alias *ALIASES;
extern size_t ALIASES_COUNT;

extern Proxy *PROXIES;
extern size_t PROXIES_COUNT;

void load_mock_config();
void free_mock_config();

#endif /* config_h */
