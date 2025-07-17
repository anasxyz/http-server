#ifndef proxy_h
#define proxy_h

#include "utils_http.h"

HttpResponse* proxy_to_backend(HttpRequest request, char *host, int port);

#endif /* proxy_h */
