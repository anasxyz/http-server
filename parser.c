#include <stdio.h>
#include <stdlib.h>
#include <strings.h> // For strcasecmp
#include <string.h>

#include "parser.h"
#include "server.h"

// A helper function to parse all headers into a structured array
static void parse_all_headers(client_state_t *client_state, char *buffer) {
    char *header_start = strstr(buffer, "\r\n") + 2;
    char *header_line;
    char *saveptr;
    
    client_state->header_count = 0;
    
    char temp_buffer[MAX_BUFFER_SIZE];
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

// A helper function to find a header value
const char* get_header_value(client_state_t *client_state, const char *key) {
    for (int i = 0; i < client_state->header_count; i++) {
        if (strcasecmp(client_state->parsed_headers[i].key, key) == 0) {
            return client_state->parsed_headers[i].value;
        }
    }
    return NULL;
}

// The main function to parse the entire request
// It now returns the next state instead of setting it directly.
client_state_enum_t parse_http_request(client_state_t *client_state) {
    char *header_end = strstr(client_state->in_buffer, "\r\n\r\n");
    if (!header_end) return STATE_READING_REQUEST; // Should not happen here, but a safety check

    // Isolate the header section for parsing
    char header_section[MAX_BUFFER_SIZE];
    size_t header_len = header_end - client_state->in_buffer;
    strncpy(header_section, client_state->in_buffer, header_len);
    header_section[header_len] = '\0';

    // Parse request line and headers
    char *request_line_end = strstr(header_section, "\r\n");
    if (request_line_end) {
        *request_line_end = '\0';
        sscanf(header_section, "%15s %1023s %15s",
               client_state->method, client_state->path, client_state->http_version);
        *request_line_end = '\r';
    }

    parse_all_headers(client_state, header_section);

    // Check for Keep-Alive
    const char *connection_header = get_header_value(client_state, "Connection");
    client_state->keep_alive = (connection_header && strcasecmp(connection_header, "keep-alive") == 0);

    // Get Content-Length for POST/PUT requests
    client_state->content_length = 0;
    const char *content_length_header = get_header_value(client_state, "Content-Length");
    if (content_length_header) {
        client_state->content_length = atoi(content_length_header);
    }
    
    char *body_data_start = header_end + 4;
    size_t initial_body_data_len = client_state->in_buffer_len - (body_data_start - client_state->in_buffer);

    // Check if a body is expected
    if (strcasecmp(client_state->method, "POST") == 0 && client_state->content_length > 0) {
        if (client_state->body_buffer == NULL) { // Allocate only once
            client_state->body_buffer = malloc(client_state->content_length + 1);
            if (!client_state->body_buffer) {
                perror("malloc for body buffer failed");
                return STATE_CLOSED; // Indicate a critical error
            }
        }
        
        memcpy(client_state->body_buffer, body_data_start, initial_body_data_len);
        client_state->body_received = initial_body_data_len;

        if (client_state->body_received >= client_state->content_length) {
            client_state->body_buffer[client_state->content_length] = '\0';
            printf("Full POST request with body received on first read.\n");
            return STATE_WRITING_RESPONSE;
        } else {
            printf("Partial POST request body received, transitioning to READING_BODY state.\n");
            return STATE_READING_BODY;
        }
    } else {
        printf("Request is complete (no body or not a POST).\n");
        return STATE_WRITING_RESPONSE;
    }
}

// Function to prepare the HTTP response
void create_http_response(client_state_t *client_state) {
    const char *http_response_body = "Hello World!";
    char http_headers[256];

    snprintf(http_headers, sizeof(http_headers),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n",
             strlen(http_response_body));

    if (client_state->keep_alive) {
        strcat(http_headers, "Connection: keep-alive\r\n");
        char keep_alive_info[64];
        snprintf(keep_alive_info, sizeof(keep_alive_info),
                 "Keep-Alive: timeout=%d, max=100\r\n", KEEPALIVE_IDLE_TIMEOUT_SECONDS);
        strcat(http_headers, keep_alive_info);
    } else {
        strcat(http_headers, "Connection: close\r\n");
    }

    snprintf(client_state->out_buffer, sizeof(client_state->out_buffer),
             "%s\r\n%s",
             http_headers, http_response_body);

    client_state->out_buffer_len = strlen(client_state->out_buffer);
    client_state->out_buffer_sent = 0;
}
