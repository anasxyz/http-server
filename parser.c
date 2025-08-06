#include <stdio.h>
#include <strings.h> // For strcasecmp

#include "parser.h"
#include "server.h"

// Function definitions
// This is the function we wrote previously to parse all headers
static void parse_all_headers(client_state_t *client_state) {
    char *header_start = strstr(client_state->in_buffer, "\r\n") + 2;
    char *header_line;
    char *saveptr;
    
    client_state->header_count = 0;
    
    char temp_buffer[4096];
    strncpy(temp_buffer, header_start, sizeof(temp_buffer) - 1);
    temp_buffer[sizeof(temp_buffer) - 1] = '\0';

    header_line = strtok_r(temp_buffer, "\r\n", &saveptr);
    while (header_line != NULL) {
        char *colon = strchr(header_line, ':');
        if (colon) {
            *colon = '\0';
            char *key = header_line;
            char *value = colon + 1;
            while (*value == ' ') value++;

            if (client_state->header_count < MAX_HEADERS) {
                strncpy(client_state->parsed_headers[client_state->header_count].key, key, MAX_HEADER_KEY_LEN - 1);
                client_state->parsed_headers[client_state->header_count].key[MAX_HEADER_KEY_LEN - 1] = '\0';

                strncpy(client_state->parsed_headers[client_state->header_count].value, value, MAX_HEADER_VALUE_LEN - 1);
                client_state->parsed_headers[client_state->header_count].value[MAX_HEADER_VALUE_LEN - 1] = '\0';
                
                client_state->header_count++;
            }
        }
        header_line = strtok_r(NULL, "\r\n", &saveptr);
    }
}

// The main function to parse the entire request
void parse_http_request(client_state_t *client_state) {
    char *request_line_end = strstr(client_state->in_buffer, "\r\n");
    if (!request_line_end) {
        return;
    }
    *request_line_end = '\0';

    char temp_buffer[4096];
    strncpy(temp_buffer, client_state->in_buffer, sizeof(temp_buffer) - 1);
    temp_buffer[sizeof(temp_buffer) - 1] = '\0';
    
    char *method = strtok(temp_buffer, " ");
    char *path = strtok(NULL, " ");
    char *version = strtok(NULL, " ");

    if (method) strncpy(client_state->method, method, sizeof(client_state->method) - 1);
    if (path) strncpy(client_state->path, path, sizeof(client_state->path) - 1);
    if (version) strncpy(client_state->http_version, version, sizeof(client_state->http_version) - 1);

    *request_line_end = '\r';

    parse_all_headers(client_state);

    const char *connection_header = get_header_value(client_state, "Connection");
    if (connection_header && strcasecmp(connection_header, "keep-alive") == 0) {
        client_state->keep_alive = 1;
    } else {
        client_state->keep_alive = 0;
    }

    printf("Parsed Request from client %d:\n", client_state->fd);
    printf("  Method: %s\n", client_state->method);
    printf("  Path: %s\n", client_state->path);
    printf("  Version: %s\n", client_state->http_version);
    printf("  Keep-Alive: %s\n", client_state->keep_alive ? "true" : "false");
    printf("  Header Count: %d\n", client_state->header_count);
    for (int i = 0; i < client_state->header_count; i++) {
        printf("    - %s: %s\n", client_state->parsed_headers[i].key, client_state->parsed_headers[i].value);
    }
}

// Helper function to get a header value
const char* get_header_value(client_state_t *client_state, const char *key) {
    for (int i = 0; i < client_state->header_count; i++) {
        if (strcasecmp(client_state->parsed_headers[i].key, key) == 0) {
            return client_state->parsed_headers[i].value;
        }
    }
    return NULL;
}

void create_http_response(client_state_t *client_state) {
    const char *http_response_body = "Hello World!";
    char http_headers[256];

    // Build the status line and standard headers
    snprintf(http_headers, sizeof(http_headers),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n",
             strlen(http_response_body));

    // Check the keep_alive flag set by the parser and append the header
    if (client_state->keep_alive) {
        strcat(http_headers, "Connection: keep-alive\r\n");
        char keep_alive_info[64];
        snprintf(keep_alive_info, sizeof(keep_alive_info),
                 "Keep-Alive: timeout=%d, max=100\r\n", KEEPALIVE_IDLE_TIMEOUT_SECONDS);
        strcat(http_headers, keep_alive_info);
    } else {
        strcat(http_headers, "Connection: close\r\n");
    }

    // Combine headers and body into the output buffer
    snprintf(client_state->out_buffer, sizeof(client_state->out_buffer),
             "%s\r\n%s",
             http_headers, http_response_body);

    client_state->out_buffer_len = strlen(client_state->out_buffer);
    client_state->out_buffer_sent = 0;
}
