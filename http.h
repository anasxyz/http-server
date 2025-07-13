#ifndef http_h
#define http_h

#include "server.h"

void send_response(int socket, const char *status, const char *content_type, const char *body);

void handle_request(int socket, char *request);

#endif /* http_h */
