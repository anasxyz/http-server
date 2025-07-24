#ifndef proxy_h
#define proxy_h

#include "config.h"
#include "utils_http.h"

typedef struct {
  char *headers;
  char *body;
} ProxyResult;

int parse_proxy_target(const char *url, char *host, size_t host_size,
                       int *port);
Proxy *find_proxy_for_path(const char *request_path);
char *strip_prefix(const char *path, const char *prefix);
ProxyResult *proxy_request(Proxy *proxy, const char *original_request_str);

#endif /* proxy_h */
