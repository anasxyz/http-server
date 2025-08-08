#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string.h>

#include "server.h"

int parse_http_request(client_state_t *client_state);
const char* get_header_value(client_state_t *client_state, const char *key);
void create_http_response(client_state_t *client_state);
void create_http_error_response(client_state_t *client_state, int status_code, const char *message);

#endif // HTTP_PARSER_H
