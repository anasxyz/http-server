#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>
#include <time.h>
#include <glib.h>

extern GHashTable *client_states_map;

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

// The different possible states for a client connection
typedef enum {
    STATE_READING_REQUEST,
    STATE_READING_BODY,
    STATE_WRITING_RESPONSE,
    STATE_IDLE,   // New state: Waiting for a new request on a keep-alive connection
    STATE_CLOSED, // New state: Connection is being closed
} client_state_enum_t;

// This struct is a central point of data for each client.
typedef struct {
    int fd;
    client_state_enum_t state;
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
    
    ssize_t timeout_heap_index; // Store the index of the client in the heap
} client_state_t;


// --- Function prototypes ---
// These are external so other files know about them
int set_nonblocking(int fd);
int setup_listening_socket(int port);
void handle_new_connection(int listen_sock, int epoll_fd);
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr);
void close_client_connection(int epoll_fd, client_state_t *client_state);
void handle_sigint(int sig);

// New state transition function
void transition_state(int epoll_fd, client_state_t *client, client_state_enum_t new_state);

#endif // SERVER_H
