#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response();

void send_response(int socket, HttpResponse *response);
void handle_request(int socket, char *request);

typedef HttpResponse *(*RequestHandler)(HttpRequest *, void *context);

typedef struct {
  const char *method;
  RequestHandler handler;
  void *context;  // for passing route info or other data
} MethodHandler;

#endif /* http_h */
