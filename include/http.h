#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

HttpResponse *create_response(int status_code, char *path);
void send_response(int socket, HttpResponse *response);
void handle_request(int socket, char *request);

#endif /* http_h */
