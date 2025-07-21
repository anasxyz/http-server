#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response(int status_code);
void handle_request(int socket, char *request);
HttpResponse *handle_get(HttpRequest *request, void *context);
HttpResponse *handle_post(HttpRequest *request, void *context);

#endif /* http_h */
