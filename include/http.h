#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response(int status_code, char *path);
void handle_request(int socket, char *request);
HttpResponse *handle_get(HttpRequest *request);
HttpResponse *handle_post(HttpRequest *request);

#endif /* http_h */
