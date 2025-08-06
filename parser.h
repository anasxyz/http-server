#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string.h> // Include necessary libraries for the functions in this header

#include "server.h" // Includes all necessary structs and constants

// Function declarations
void parse_http_request(client_state_t *client_state);
const char* get_header_value(client_state_t *client_state, const char *key);
void create_http_response(client_state_t *client_state);

#endif // HTTP_PARSER_H
