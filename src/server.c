#include <arpa/inet.h>
#include <atomic_ops.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

#define MAX_EVENTS 1024
#define MAX_BUFFER_SIZE 8192

GHashTable *client_map;

volatile sig_atomic_t shutdown_flag = 0;

void sigint_handler(int signum) { shutdown_flag = 1; }

atomic_int *total_connections;

typedef struct {
  char *key;
  char *value;
} header_t;

typedef struct {
  char *method;
  char *uri;
  char *version;
} request_line_t;

typedef struct {
  request_line_t request_line;
  GHashTable *headers;
  size_t num_headers;
  char *body;
  size_t body_len;
} request_t;

typedef struct {
  char *version;
  int status_code;
  char *status_message;
} response_line_t;

typedef struct {
  response_line_t response_line;
  header_t *headers;
  size_t num_headers;
} response_t;

typedef enum {
  READING,
  WRITING,
  CLOSING,
} state_e;

typedef struct {
  // fields for socket
  int fd;
  char *ip;

  // fields for receiving data
  char *in_buffer;
  size_t in_buffer_len;
  size_t in_buffer_size;

  // fields for sending data
  char out_buffer[MAX_BUFFER_SIZE];
  size_t out_buffer_len;
  size_t out_buffer_sent;

  // fields for state
  state_e state;

  // fields for sending headers
  bool headers_sent;

  // fields for sending files
  int file_fd;
  off_t file_offset;
  off_t file_size;

  server_config *parent_server; // pointer to parent server config

  request_t *request;
  response_t *response;
} client_t;

// forward declarations of all functions in this file
void transition_state(client_t *client, int epoll_fd, state_e new_state);
int set_epoll(int epoll_fd, client_t *client, uint32_t epoll_events);
int handle_new_connection(int connection_socket, int epoll_fd,
                          int *listen_sockets, int *active_connections);
void close_connection(client_t *client, int epoll_fd, int *active_connections);

int read_client_request(client_t *client);

void serve_file(client_t *client, int epoll_fd);
void run_worker(int *listen_sockets, int num_sockets);
void init_sockets(int *listen_sockets);
void fork_workers(int *listen_sockets);
void server_cleanup(int *listen_sockets);
void check_valid_config();

void free_client(client_t *client);

// ##############################################################################
// ## state transitions
// ##############################################################################

const char *state_to_string(state_e state) {
  switch (state) {
  case READING:
    return "READING";
  case WRITING:
    return "WRITING";
  case CLOSING:
    return "CLOSING";
  default:
    return "UNKNOWN";
  }
}

void transition_state(client_t *client, int epoll_fd, state_e new_state) {
  state_e current_state = client->state;

  // Check if the state is already the new state
  if (current_state == new_state) {
    return;
  }

  // Handle epoll modifications based on the transition
  if (current_state == READING && new_state == WRITING) {
    if (set_epoll(epoll_fd, client, EPOLLOUT) == -1) {
      return;
    }
  } else if (current_state == WRITING && new_state == READING) {
    if (set_epoll(epoll_fd, client, EPOLLIN) == -1) {
      return;
    }
  }

  client->state = new_state;

  logs('D', "Transitioned connection %s from %s to %s.", NULL, client->ip,
       state_to_string(current_state), state_to_string(new_state));

  if (new_state == CLOSING) {
  }
}

int set_epoll(int epoll_fd, client_t *client, uint32_t epoll_events) {
  struct epoll_event event;
  event.events = epoll_events;
  event.data.fd = client->fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event) == -1) {
    return -1;
  }
  return 0;
}

// ##############################################################################
// ## connection handling
// ##############################################################################

void initialise_client(client_t *client) {
  // initialise socket fields
  client->fd = -1;
  client->ip = NULL;

  // initialise receiving data fields
  client->in_buffer = NULL;
  client->in_buffer_len = 0;

  // initialise sending data fields
  memset(client->out_buffer, 0, MAX_BUFFER_SIZE);
  client->out_buffer_len = 0;
  client->out_buffer_sent = 0;

  // initialise state fields
  client->state = READING;

  // initialise headers fields
  client->headers_sent = false;

  // initialise file fields
  client->file_fd = -1;
  client->file_offset = 0;
  client->file_size = 0;
}

int handle_new_connection(int connection_socket, int epoll_fd,
                          int *listen_sockets, int *active_connections) {
  struct sockaddr_in client_address;
  socklen_t client_address_len;
  struct epoll_event event;
  client_address_len = sizeof(client_address);

  // accept client's connection
  int client_sock =
      accept(connection_socket, (struct sockaddr *)&client_address,
             &client_address_len);
  if (client_sock == -1) {
    // if error is EAGAIN or EWOULDBLOCK, another worker handled it so just
    // continue like normal
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }

    // for anything else, it's a real problem
    logs('E', "Failed to accept connection.",
         "handle_new_connection(): accept() failed.");
    return -1;
  }

  // if max connections reached, reject connection
  if (atomic_load(total_connections) >= global_config->max_connections) {
    logs('W', "Server is full. Rejecting connection.", NULL);
    close(client_sock); // immediately close socket
    return 0;
  }

  // set client's socket to non-blocking
  set_nonblocking(client_sock);

  // allocate and initialise new client struct for this connection
  client_t *client = malloc(sizeof(client_t));

  // initialise client struct to all zeros
  initialise_client(client);

  // set needed client fields
  client->fd = client_sock;
  client->ip = strdup(inet_ntoa(client_address.sin_addr));

  // find the server config for this connection
  for (int i = 0; i < global_config->http->num_servers; i++) {
    if (connection_socket == listen_sockets[i]) {
      client->parent_server = &global_config->http->servers[i];
      break;
    }
  }

  // insert client into global map
  g_hash_table_insert(client_map, GINT_TO_POINTER(client->fd), client);

  // add client's socket to epoll instance
  event.events = EPOLLIN;
  event.data.fd = client->fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client->fd, &event) == -1) {
    logs('E', "Failed to add client socket to epoll.",
         "handle_new_connection(): epoll_ctl() failed.");
    close(client->fd);
    free_client(client);
    return -1;
  }

  logs('I', "Accepted connection %s (socket %d) on server %s:%d", NULL,
       client->ip, client->fd, client->parent_server->server_names[0],
       client->parent_server->listen_port);

  (*active_connections)++;
  atomic_fetch_add(total_connections, 1);

  logs('I', "Total active connections: %d", NULL,
       atomic_load(total_connections));

  logs('D', "Transitioned connection %s from NOTHING to READING.", NULL,
       client->ip);

  printf("Total active connections: %d\n", atomic_load(total_connections));

  return 0;
}

void close_connection(client_t *client, int epoll_fd, int *active_connections) {
  if (client == NULL) {
    return;
  }

  logs('I', "Closing connection for client %s (socket %d)", NULL, client->ip,
       client->fd);

  // remove the file descriptor from the epoll instance
  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL) == -1 &&
      errno != ENOENT) {
    perror("REASON: epoll_ctl: DEL client socket failed");
  }

  // close the socket file descriptor
  close(client->fd);

  // remove the client from the hash table
  // this will trigger the g_hash_table_new_full's
  // GDestroyNotify, which will call free_client(),
  // thereby freeing the client_t struct and its contents
  g_hash_table_remove(client_map, GINT_TO_POINTER(client->fd));

  (*active_connections)--;
  atomic_fetch_sub(total_connections, 1);

  logs('I', "Total active connections: %d", NULL,
       atomic_load(total_connections));

  printf("Total active connections: %d\n", atomic_load(total_connections));
}

// ##############################################################################
// ## request handling
// ##############################################################################

void free_request(request_t *request) {
  if (request == NULL) {
    return;
  }

  // Free the dynamically allocated strings for the request line
  free(request->request_line.method);
  free(request->request_line.uri);
  free(request->request_line.version);

  // The GHashTable's destroy function will free its own keys and values
  if (request->headers != NULL) {
    g_hash_table_destroy(request->headers);
  }

  // Free the request body, if it exists
  free(request->body);

  // Finally, free the request struct itself
  free(request);
}

// helper function to find a newline sequence
char *find_newline(char *buffer, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (buffer[i] == '\r' && (i + 1) < len && buffer[i + 1] == '\n') {
      return &buffer[i];
    }
  }
  return NULL;
}
int parse_request(char *buffer, size_t buffer_len, request_t *request) {
  char *current_pos = buffer;
  char *end_of_line;

  // parse request line
  end_of_line = find_newline(current_pos, buffer_len - (current_pos - buffer));
  if (end_of_line == NULL) {
    return -1; // malformed request line
  }

  *end_of_line = '\0'; // null terminate request line

  char method[16], uri[256], version[16];
  if (sscanf(current_pos, "%s %s %s", method, uri, version) != 3) {
    return -1; // malformed request line
  }

  request->request_line.method = strdup(method);
  request->request_line.uri = strdup(uri);
  request->request_line.version = strdup(version);
  current_pos = end_of_line + 2;

  // parse headers
  request->headers =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  if (request->headers == NULL) {
    return -1; // failed to allocate memory for headers
  }

  while ((end_of_line =
              find_newline(current_pos, buffer_len - (current_pos - buffer)))) {
    *end_of_line = '\0'; // null terminate header line

    // check for end of headers
    if (strlen(current_pos) == 0) {
      current_pos = end_of_line + 2;
      break;
    }

    char *colon_pos = strchr(current_pos, ':');
    if (colon_pos == NULL) {
      return -1; // malformed header
    }

    *colon_pos = '\0'; // null terminate header name
    char *key = current_pos;
    char *value = trim(colon_pos + 1);

    g_hash_table_insert(request->headers, g_strdup(key), g_strdup(value));

    current_pos = end_of_line + 2;
  }

  // handle body
  const gchar *content_length_str =
      g_hash_table_lookup(request->headers, "Content-Length");
  if (content_length_str) {
    request->body_len = atoi(content_length_str);
    if (current_pos + request->body_len <= buffer + buffer_len) {
      request->body = malloc(request->body_len + 1);
      memcpy(request->body, current_pos, request->body_len);
      request->body[request->body_len] = '\0';
    } else {
      g_hash_table_destroy(request->headers);
      return -1; // malformed request
    }
  }

  return 0;
}

int read_client_request(client_t *client) {
  char temp_buffer[MAX_BUFFER_SIZE];
  ssize_t bytes_read;
  size_t headers_len = 0;
  size_t body_len = 0;

  while (1) {
    bytes_read = read(client->fd, temp_buffer, MAX_BUFFER_SIZE);

    if (bytes_read == 0) {
      return -1; // Client closed connection
    }

    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No more data to read for now, so return 1 to not change state
        return 1;
      }
      return -1; // A real error
    }

    // Allocate memory for in_buffer if it's the first read
    if (client->in_buffer == NULL) {
      client->in_buffer_size = bytes_read;
      client->in_buffer = malloc(client->in_buffer_size + 1);
      if (client->in_buffer == NULL) {
        logs('E', "Failed to allocate memory for input buffer", NULL);
        return -1;
      }
    } else {
      // Reallocate memory for larger requests
      if (client->in_buffer_len + bytes_read + 1 > client->in_buffer_size) {
        client->in_buffer_size = client->in_buffer_len + bytes_read + 1;
        char *temp = realloc(client->in_buffer, client->in_buffer_size);
        if (temp == NULL) {
          logs('E', "Failed to reallocate memory for input buffer", NULL);
          return -1;
        }
        client->in_buffer = temp;
      }
    }

    // Copy new data to client's buffer
    memcpy(client->in_buffer + client->in_buffer_len, temp_buffer, bytes_read);
    client->in_buffer_len += bytes_read;
    client->in_buffer[client->in_buffer_len] = '\0';

    // Check if we have received a complete request
    char *end_of_headers = strstr(client->in_buffer, "\r\n\r\n");
    if (end_of_headers) {
      if (headers_len == 0) {
        headers_len = (end_of_headers - client->in_buffer) + 4;

        // Search for Content-Length header to determine body size
        char *content_length_header =
            strstr(client->in_buffer, "Content-Length:");
        if (content_length_header) {
          char *value_start = content_length_header + strlen("Content-Length:");
          body_len = (size_t)atoi(value_start);
        }
      }

      // Check if the entire request (headers + body) has been received
      if (client->in_buffer_len >= headers_len + body_len) {
        // A complete request is now in the buffer
        client->request = malloc(sizeof(request_t));
        if (client->request == NULL) {
          logs('E', "Failed to allocate memory for request_t", NULL);
          return -1;
        }

        if (parse_request(client->in_buffer, client->in_buffer_len,
                          client->request) == 0) {
          // Success! The request is parsed.
          // Reset buffer for the next request
          client->in_buffer_len = 0;
          return 0; // Return 0 to trigger state change to WRITING
        } else {
          // Parsing failed
          free_request(client->request);
          client->request = NULL;
          return -1;
        }
      }
    }
  }
}

// ##############################################################################
// ## response handling
// ##############################################################################

// Handles partial writes for a generic buffer.
// Returns 0 on success, 1 on EAGAIN (partial write), -1 on fatal error.
int write_partial(client_t *client, const char *buffer, size_t *offset,
                  size_t len) {
  size_t remaining = len - *offset;
  if (remaining == 0) {
    return 0;
  }

  ssize_t bytes_written = write(client->fd, buffer + *offset, remaining);

  if (bytes_written < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      logs('D', "Partial write to client %s", NULL, client->ip);
      return 1;
    }
    return -1;
  }

  *offset += bytes_written;

  if (*offset >= len) {
    return 0;
  }
  return 1;
}

// sends a file using sendfile()
// returns 0 on success, 1 on EAGAIN (partial write), -1 on fatal error
int send_file_body(client_t *client) {
  if (client->file_offset >= client->file_size) {
    return 0;
  }

  ssize_t bytes_sent =
      sendfile(client->fd, client->file_fd, &client->file_offset,
               client->file_size - client->file_offset);

  if (bytes_sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 1;
    }
    return -1;
  }

  if (client->file_offset >= client->file_size) {
    return 0;
  }
  return 1;
}

// builds an HTTP header string into the provided buffer
void build_response_headers(char *buffer, size_t buffer_size,
                            const char *content_type, size_t content_length) {
  const char *template = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: keep-alive\r\n"
                         "\r\n";
  snprintf(buffer, buffer_size, template, content_type, content_length);
}

// handles building and sending a complete response with a small body
int send_simple_response(client_t *client, const char *body,
                         const char *content_type) {
  size_t body_len = strlen(body);

  if (client->out_buffer_len == 0) {
    build_response_headers(client->out_buffer, MAX_BUFFER_SIZE, content_type,
                           body_len);
    size_t header_len = strlen(client->out_buffer);

    if (header_len + body_len >= MAX_BUFFER_SIZE) {
      logs('E', "Response body too large for buffer.", NULL);
      return -1;
    }

    memcpy(client->out_buffer + header_len, body, body_len);
    client->out_buffer_len = header_len + body_len;
    client->out_buffer_sent = 0;
  }

  int write_status =
      write_partial(client, client->out_buffer, &client->out_buffer_sent,
                    client->out_buffer_len);

  if (write_status != 0) {
    return write_status;
  }

  client->out_buffer_sent = 0;
  client->out_buffer_len = 0;
  return 0;
}

// handles sending a file, first the headers, then the file content
int send_file_response(client_t *client) {
  if (!client->headers_sent) {
    if (client->out_buffer_len == 0) {
      build_response_headers(client->out_buffer, MAX_BUFFER_SIZE,
                             "application/octet-stream", client->file_size);
      client->out_buffer_len = strlen(client->out_buffer);
    }

    int headers_status =
        write_partial(client, client->out_buffer, &client->out_buffer_sent,
                      client->out_buffer_len);
    if (headers_status != 0) {
      return headers_status;
    }
    client->headers_sent = true;
    client->out_buffer_sent = 0;
  }

  int file_status = send_file_body(client);
  if (file_status != 0) {
    return file_status;
  }

  close(client->file_fd);
  client->file_fd = -1;
  client->file_offset = 0;
  client->file_size = 0;
  client->headers_sent = false;
  client->out_buffer_len = 0;
  return 0;
}

// main orchestrator function
int write_client_response(client_t *client) {
  // TODO: use client->request here
  const char *response_body = "Hello World";
  const char *content_type = "text/plain";

  // check if there is a file to be sent
  if (client->file_fd > 0) {
    return send_file_response(client);
  }
  // otherwise, handle a simple body response
  else {
    return send_simple_response(client, response_body, content_type);
  }
}

// ##############################################################################
// ## server process handling
// ##############################################################################

int setup_epoll(int *listen_sockets, int num_sockets) {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    logs('E', "Worker failed to create epoll instance.",
         "run_worker(): epoll_create1() failed.");
    exits();
  }

  // add the listening sockets to the epoll instance
  struct epoll_event event;
  for (int i = 0; i < num_sockets; i++) {
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    event.data.fd = listen_sockets[i];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sockets[i], &event) == -1) {
      logs('E', "Worker failed to register listening socket.",
           "run_worker(): epoll_ctl() failed.");
      close(epoll_fd);
      exits();
    }
  }

  return epoll_fd;
}

void run_worker(int *listen_sockets, int num_sockets) {
  // setup epoll
  struct epoll_event events[MAX_EVENTS];
  int num_events;
  int epoll_fd = setup_epoll(listen_sockets, num_sockets);

  int active_connections = 0;

  // create client_map
  client_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                     (GDestroyNotify)free_client);
  if (client_map == NULL) {
    logs('E', "Worker failed to create client_map.",
         "run_worker(): g_hash_table_new() failed.");
    exits();
  }

  while (shutdown_flag == 0) {
    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events == -1) {
      if (errno == EINTR) {
        if (shutdown_flag == 1) {
          break;
        }
        continue;
      }
      logs('E', "Worker's epoll_wait failed.",
           "run_worker(): epoll_wait() failed.");
      exits();
    }

    for (int i = 0; i < num_events; i++) {
      int current_fd = events[i].data.fd;

      // check if event is on a listening socket
      int is_listening_socket = 0;
      for (int j = 0; j < num_sockets; j++) {
        if (current_fd == listen_sockets[j]) {
          is_listening_socket = 1;
          break;
        }
      }

      if (is_listening_socket) {
        // handle new connection
        if (handle_new_connection(current_fd, epoll_fd, listen_sockets,
                                  &active_connections) == -1) {
          logs('W', "Server is full. Rejecting connection.", NULL);
        }
      } else {
        // get client from client map
        client_t *client = (client_t *)g_hash_table_lookup(
            client_map, GINT_TO_POINTER(current_fd));

        if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
          transition_state(client, epoll_fd, CLOSING);
          close_connection(client, epoll_fd, &active_connections);
        } else if (events[i].events & EPOLLIN) {
          // it's a read event
          int read_status = read_client_request(client);
          if (read_status == 0) {
            // read returns 0, which means success
            // transition epoll event to EPOLLOUT and set state to WRITING
            logs('D', "No more data to read from Client %s", NULL, client->ip);
            transition_state(client, epoll_fd, WRITING);
          } else if (read_status == 1) {
            // read returns 1, which means more data is expected, stay in
            // READING state
            logs('D', "More data to read from Client %s", NULL, client->ip);
            continue;
          } else {
            // read returns -1, which means error, so close connection
            logs('E', "read_client_request() failed. Output: %s", NULL,
                 read_status);
            transition_state(client, epoll_fd, CLOSING);
            close_connection(client, epoll_fd, &active_connections);
          }
        } else if (events[i].events & EPOLLOUT) {
          // it's a write event
          int write_status = write_client_response(client);
          if (write_status == 0) {
            // write returns 0, which means success
            // transition epoll event to EPOLLIN and set state to READING
            int keepalive = 1; // obviously would be replaced when we actually
                               // pase the keep-alive header
            logs('D', "No more data to write to Client %s", NULL, client->ip);
            if (keepalive == 1) {
              transition_state(client, epoll_fd, READING);
            } else {
              transition_state(client, epoll_fd, CLOSING);
              close_connection(client, epoll_fd, &active_connections);
            }
          } else {
            // write returns -1, which means error, so close connection
            logs('E', "write_client_response() failed. Output: %s", NULL,
                 write_status);
            transition_state(client, epoll_fd, CLOSING);
            close_connection(client, epoll_fd, &active_connections);
          }
        } else {
          logs('E', "Unexpected event on socket %d", NULL, current_fd);
          close_connection(client, epoll_fd, &active_connections);
        }
      }
    }
  }

  close(epoll_fd);
  g_hash_table_destroy(client_map);
  atexit(free_config);
  munmap(total_connections, sizeof(atomic_int));
  exit(0);
}

void init_sockets(int *listen_sockets) {
  server_config *servers = global_config->http->servers;

  for (int i = 0; i < global_config->http->num_servers; i++) {
    listen_sockets[i] = setup_listening_socket(servers[i].listen_port);
    logs('I', "%s listening on port %d.", NULL, servers[i].server_names[0],
         servers[i].listen_port);
    if (listen_sockets[i] == -1) {
      exits();
    }
  }
}

void fork_workers(int *listen_sockets) {
  // fork worker processes
  for (int i = 0; i < global_config->worker_processes; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      logs('E', "Failed to fork worker process.", NULL);
      exits();
    }
    // child process
    if (pid == 0) {
      logs('I', "Worker process %d started (pid %d).", NULL, i, getpid());
      run_worker(listen_sockets, global_config->http->num_servers);
      exits();
    }
  }
}

void server_cleanup(int *listen_sockets) {
  // master process waits for all childen to finish
  // TODO: handle signals
  for (int i = 0; i < global_config->worker_processes; i++) {
    wait(NULL);
  }

  // clean up
  for (int i = 0; i < global_config->http->num_servers; i++) {
    close(listen_sockets[i]);
  }
}

void check_valid_config() {
  server_config *servers = global_config->http->servers;
  int num_servers = global_config->http->num_servers;

  // --- global config checks ---
  if (num_servers == 0) {
    logs('E', "No servers configured.", NULL);
    exits();
  }
  if (global_config->worker_processes == 0) {
    logs('E', "No worker processes configured.", NULL);
    exits();
  }

  // --- server config checks ---
  for (int i = 0; i < num_servers; i++) {
    if (!servers[i].server_names[0]) {
      char default_name[32];
      snprintf(default_name, sizeof(default_name), "server%d", i + 1);
      servers[i].server_names[0] = strdup(default_name);
      logs('W', "No name configured for server %d. Setting to default: %s",
           NULL, i + 1, default_name);
    }

    if (!servers[i].listen_port) {
      logs('E', "No port configured for server %s", NULL,
           servers[i].server_names[0]);
      exits();
    }
    if (servers[i].listen_port < 1024) {
      logs('E',
           "Invalid port number. Configure a different port for %s (>1024).",
           NULL, servers[i].server_names[0]);
      exits();
    }
  }
}

void free_client(client_t *client) {
  if (client == NULL) {
    return;
  }

  // Free dynamically allocated string
  free(client->ip);

  // Close the file descriptor for the file being served, if it's open
  if (client->file_fd != -1) {
    close(client->file_fd);
  }

  // Free the parsed request data, if it exists
  free_request(client->request);

  if (client->in_buffer != NULL) {
    free(client->in_buffer);
  }

  // Free the struct itself
  free(client);
}

void clear_log_file() {
  FILE *log_file = fopen("logs.log", "w");
  if (log_file) {
    fclose(log_file);
  }
}

int main() {
  clear_log_file();

  logs('I', "Starting server...", NULL);
  signal(SIGINT, sigint_handler);

  const char *shm_name = "/server_connections";
  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("ERROR: shm_open failed");
    exit(EXIT_FAILURE);
  }
  ftruncate(shm_fd, sizeof(atomic_int));

  total_connections = mmap(NULL, sizeof(atomic_int), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
  if (total_connections == MAP_FAILED) {
    perror("ERROR: mmap failed");
    exit(EXIT_FAILURE);
  }

  atomic_store(total_connections, 0);

  // 1. load config
  load_config();

  // 2. check if config is valid
  check_valid_config();

  // 3. setup listening sockets
  int listen_sockets[global_config->http->num_servers];

  // 4. fill socket array with server block sockets
  init_sockets(listen_sockets);

  // 5. fork worker processes
  fork_workers(listen_sockets);

  // 6. clean up server resources
  server_cleanup(listen_sockets);

  // 7. free_config at exit for master process
  atexit(free_config);
  shm_unlink(shm_name);
  logs('I', "Server exiting.", NULL);

  return 0;
}
