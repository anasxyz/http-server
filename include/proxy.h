#ifndef proxy_h
#define proxy_h

#include "utils_http.h"

#define BACKEND_HOST "127.0.0.1"
#define BACKEND_PORT 5050

HttpResponse* proxy_to_backend(HttpRequest request);

#endif /* proxy_h */
