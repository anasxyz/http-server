#ifndef _SERVER_H_
#define _SERVER_H_

#include <glib.h>
#include <stdint.h>

#include "config.h"
#include "util.h"

// max epoll events per epoll_wait return
#define MAX_EVENTS 1024

// max buffer size for reading/writing
// probably obsolete after switching to dynamic buffers
#define MAX_BUFFER_SIZE 8192 // 8KB

// request line struct
// eg GET /index.html HTTP/1.1
typedef struct {
  char *method;
  char *uri;
  char *version;
} request_line_t;

// request struct
typedef struct {
  request_line_t request_line;
  GHashTable *headers;
  size_t num_headers;
  char *body;
  size_t body_len;
} request_t;

// client state enum
typedef enum {
  READING,
  WRITING,
  CLOSING,
} state_e;

// client struct
typedef struct {
  // fields for socket
  int fd;
  char *ip;

  // fields for receiving data
  char *in_buffer;
  size_t in_buffer_len;
  size_t in_buffer_size;

  // fields for sending data
  char *out_buffer;
  size_t out_buffer_len;
  size_t out_buffer_sent;

  // fields for state
  state_e state;

  // fields for sending headers
  bool headers_sent;

  // fields for sending files
  int file_fd;
  char *file_path;
  off_t file_offset;
  off_t file_size;

	// pointer to parent server config
  server_config *parent_server;
	
	// pointer to client's parsed request
  request_t *request;
} client_t;

//
// STATE MACHINE
//

const char *state_to_string(state_e state);
void transition_state(client_t *client, int epoll_fd, state_e new_state);
int set_epoll(int epoll_fd, client_t *client, uint32_t epoll_events);

//
// CONNECTION HANDLING
//

void initialise_client(client_t *client);
int handle_new_connection(int connection_socket, int epoll_fd,
                          int *listen_sockets, int *active_connections);
void close_connection(client_t *client, int epoll_fd, int *active_connections);

//
// REQUEST HANDLING
//

char *find_newline(char *buffer, size_t len);
void initialise_request(request_t *request);
int parse_request(char *buffer, size_t buffer_len, request_t *request);
static ssize_t reads(client_t *client, char *buffer);
static int append_to_buffer(client_t *client, const char *data, size_t len);
int read_client_request(client_t *client);

//
// RESPONSE HANDLING
//

int is_directory(const char *path);
int find_file(client_t *client);
int writes(client_t *client, const char *buffer, size_t *offset, size_t len);
int build_headers(client_t *client, int status_code, const char *status_message,
                  const char *content_type, size_t content_length);
int send_headers(client_t *client, int status_code, const char *status_message,
                 const char *content_type, size_t content_length);
static int send_file_with_sendfile(client_t *client);
static int send_file_with_writes(client_t *client);
int serve_file(client_t *client, int use_sendfile);
int send_body(client_t *client, const char *body, size_t body_len);
int write_client_response(client_t *client);

//
// SERVER PROCESS HANDLING
//

int setup_epoll(int *listen_sockets, int num_sockets);
void run_worker(int *listen_sockets, int num_sockets);
void init_sockets(int *listen_sockets);
void fork_workers(int *listen_sockets);
void sock_pcs_cleanup(int *listen_sockets);
void check_valid_config();
void frees(void *ptr);
void free_request(request_t *request);
void free_client(client_t *client);
void clear_log_file();
void sigint_handler(int signum);

#endif // _SERVER_H_
