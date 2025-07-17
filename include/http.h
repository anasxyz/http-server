#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response(int status_code, char *path);
HttpResponse *create_dynamic_response(int status_code, const char *content_type, char *body, size_t body_length);

void send_response(int socket, HttpResponse *response);
void handle_request(int socket, char *request);

#define FALLBACK_500                                                           \
  "<html><head><title>500 Internal Server Error</title></head><body><h1>500 "  \
  "Internal Server Error</h1></body></html>"

#define FALLBACK_404                                                           \
  "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1>" \
  "</body></html>"

#endif /* http_h */
