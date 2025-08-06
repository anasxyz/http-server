// server.h

#ifndef SERVER_H
#define SERVER_H

#include <time.h>

// Shared constants
#define PORT 8080
#define MAX_EVENTS 10
#define MAX_BUFFER_SIZE 4096
#define MAX_ACTIVE_CLIENTS 100
#define KEEPALIVE_IDLE_TIMEOUT_SECONDS 5

#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 128
#define MAX_HEADER_VALUE_LEN 1024

// Shared structures
typedef struct {
    char key[128];
    char value[1024];
} http_header_t;

typedef enum {
    READING_REQUEST,
    READING_BODY, // NEW state for body parsing
    WRITING_RESPONSE,
} client_state_enum;

// This struct is a central point of data for each client.
typedef struct client_state_t {
    int fd;
    client_state_enum state;
    char in_buffer[MAX_BUFFER_SIZE];
    size_t in_buffer_len;
    char out_buffer[MAX_BUFFER_SIZE];
    size_t out_buffer_len;
    size_t out_buffer_sent;
    int keep_alive;
    time_t last_activity_time;

    // Parsed request data
    char method[16];
    char path[1024];
    char http_version[16];
    http_header_t parsed_headers[MAX_HEADERS];
    int header_count;

		char *body_buffer; // Dynamically allocated buffer for the body
		size_t body_buffer_size; // The current size of the body buffer
		size_t content_length;   // Total expected body length from Content-Length header
		size_t body_received;    // Number of body bytes read so far
} client_state_t;

// Function prototypes would also go here.

#endif // SERVER_H
