#ifndef SERVER_H
#define SERVER_H

#include <glib.h>
#include <openssl/types.h>
#include <stdatomic.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <time.h>

extern GHashTable *client_states_map;

// Shared constants
#define PORT 8080
#define MAX_EVENTS 1024
#define NUM_WORKERS 4 // For a 4-core CPU, as an example
#define MAX_CONNECTIONS_PER_WORKER 100
#define MAX_BUFFER_SIZE 8192
#define KEEPALIVE_IDLE_TIMEOUT_SECONDS 5
#define HTTP_PORT 8080
#define HTTPS_PORT 8443

#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 128
#define MAX_HEADER_VALUE_LEN 1024

#define MAX_BODY_SIZE 1024 * 1024 // 1MB for now idk

#define VERBOSE_MODE 1

// Global flag to control the main server loop
extern volatile int running;

// This is the pointer to the shared, atomic total connection count.
extern atomic_int *total_connections;

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
  STATE_IDLE, // New state: Waiting for a new request on a keep-alive connection
  STATE_CLOSED, // New state: Connection is being closed
  STATE_TLS_HANDSHAKE
} client_state_enum_t;

typedef enum {
    CONNECTION_TYPE_HTTP,
    CONNECTION_TYPE_HTTPS
} connection_type_t;

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

  char *body_buffer;       // Dynamically allocated buffer for the body
  size_t body_buffer_size; // The current size of the body buffer
  size_t
      content_length;   // Total expected body length from Content-Length header
  size_t body_received; // Number of body bytes read so far

  ssize_t timeout_heap_index; // Store the index of the client in the heap

  SSL *ssl;
  connection_type_t connection_type; // New member
} client_state_t;

int setup_listening_socket(int port);
void cleanup_client_state_on_destroy(gpointer data);
void handle_sigint();

#endif // SERVER_H
