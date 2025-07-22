#ifndef config_h
#define config_h

#include <stddef.h>

typedef struct {
  char* from; // request prefix like "/images/"
  char* to; // file path like "/var/www/stuff/images/"
} Alias;

typedef struct {
  char *from;  // request prefix like "/api/"
  char *to;    // backend base URL like "http://localhost:5000/"
} Proxy;

extern int PORT;
extern char* ROOT;
extern char **INDEX_FILES;
extern size_t INDEX_FILES_COUNT;

extern char **TRY_FILES;
extern size_t TRY_FILES_COUNT;

extern Alias *ALIASES;
extern size_t ALIASES_COUNT;

extern Proxy *PROXIES;
extern size_t PROXIES_COUNT;

void load_config(const char *path);
void free_config();

#endif /* config_h */
