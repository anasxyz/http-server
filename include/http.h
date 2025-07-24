#ifndef http_h
#define http_h

#include "utils_http.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

#include "client.h"

HttpResponse *create_response(int status_code, char *path);
HttpResponse *handle_get(HttpRequest *request);
HttpResponse *handle_post(HttpRequest *request);
void handle_request(ClientState *client_state);

#endif /* http_h */
