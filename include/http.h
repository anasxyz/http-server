#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response(int status_code, char *path);

void send_response(int socket, HttpResponse *response);
void handle_request(int socket, char *request);

typedef HttpResponse *(*RequestHandler)(HttpRequest *, void *context);

typedef struct {
  const char *method;
  RequestHandler handler;
  void *context;  // for passing route info or other data
} MethodHandler;

#define FALLBACK_500                                                           \
  "<html><head><title>500 Internal Server Error</title></head><body><h1>500 "  \
  "Internal Server Error</h1></body></html>"

#define FALLBACK_404                                                           \
  "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1>" \
  "</body></html>"

#endif /* http_h */
