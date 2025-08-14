#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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
  header_t *headers;
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
  int fd;
  char *ip;

  char in_buffer[MAX_BUFFER_SIZE];
  size_t in_buffer_len;

  char out_buffer[MAX_BUFFER_SIZE];
  size_t out_buffer_len;
  size_t out_buffer_sent;

  state_e state;

  request_t *request;
  response_t *response;
} client_t;

// forward declarations of all functions in this file
void transition_state(client_t *client, int epoll_fd, state_e new_state);
int set_epoll(int epoll_fd, client_t *client, uint32_t epoll_events);
int handle_new_connection(int connection_socket, int epoll_fd);
void close_connection(client_t *client, int epoll_fd);

int read_client_request(client_t *client);

void send_headers(client_t *client, int epoll_fd);
void write_client_response(client_t *client, int epoll_fd);
int write_response(int sock, const char *response_msg);

void serve_file(client_t *client, int epoll_fd);
void run_worker(int *listen_sockets, int num_sockets);
void init_sockets(int *listen_sockets);
void fork_workers(int *listen_sockets);
void server_cleanup(int *listen_sockets);
void check_valid_config();

// ##############################################################################
// ## state transitions
// ##############################################################################

void transition_state(client_t *client, int epoll_fd, state_e new_state) {
  state_e current_state = client->state;
  struct epoll_event event;

  switch (current_state) {
  case READING:
    if (set_epoll(epoll_fd, client, EPOLLOUT) == -1) {
      break;
    }
    client->state = new_state;
    logs('I', "Transitioned connection %s from READING to WRITING.", NULL,
         client->ip);

    break;
  case WRITING:
    if (set_epoll(epoll_fd, client, EPOLLIN) == -1) {
      break;
    }
    client->state = new_state;
    logs('I', "Transitioned connection %s from WRITING to READING.", NULL,
         client->ip);
    break;
  case CLOSING:
    close_connection(client, epoll_fd);
    logs('I', "Transitioned connection %s to CLOSING.", NULL, client->ip);
    break;
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

int handle_new_connection(int connection_socket, int epoll_fd) {
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

  // set client's socket to non-blocking
  set_nonblocking(client_sock);

  // allocate and initialise new client struct for this connection
  client_t *client = malloc(sizeof(client_t));
  client->fd = client_sock;
  client->ip = strdup(inet_ntoa(client_address.sin_addr));
  memset(client->in_buffer, 0, MAX_BUFFER_SIZE);
  client->in_buffer_len = 0;
  memset(client->out_buffer, 0, MAX_BUFFER_SIZE);
  client->out_buffer_len = 0;
  client->out_buffer_sent = 0;
  client->state = READING;
  client->request = NULL;
  client->response = NULL;

  // insert client into global map
  g_hash_table_insert(client_map, GINT_TO_POINTER(client->fd), client);

  // add client's socket to epoll instance
  event.events = EPOLLIN;
  event.data.fd = client->fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client->fd, &event) == -1) {
    logs('E', "Failed to add client socket to epoll.",
         "handle_new_connection(): epoll_ctl() failed.");
    close(client->fd);
    return -1;
  }

  logs('I', "Accepted connection %s (socket %d).", NULL, client->ip,
       client->fd);

	logs('I', "Transitioned connection %s from NOTHING to READING.", NULL, client->ip);

  return 0;
}

void close_connection(client_t *client, int epoll_fd) {
  // if client doesn't exist in the clients map then can't remove
  if (!g_hash_table_contains(client_map, GINT_TO_POINTER(client->fd))) {
    return;
  }

  // remove the socket from the epoll instance
  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL) == -1 &&
      errno != ENOENT) {
    perror("REASON: epoll_ctl: DEL client socket failed");
  }

  // close the file descriptor.
  close(client->fd);

  // remove client from client map
  g_hash_table_remove(client_map, GINT_TO_POINTER(client->fd));

  logs('I', "Closed connection for client %s (socket %d)", NULL, client->ip,
       client->fd);
}

// ##############################################################################
// ## request handling
// ##############################################################################

int read_client_request(client_t *client) {
  char temp_buffer[MAX_BUFFER_SIZE];
  ssize_t bytes_read;

  // keep reading until we get a full request
  while (1) {
    // read the raw request into temporary buffer
    bytes_read = read(client->fd, temp_buffer, MAX_BUFFER_SIZE);

    // if read() return 0, it means the client closed the connection
    if (bytes_read == 0) {
      return -1;
    }

    // if read() returns -1, it means there's an error
    if (bytes_read == -1) {
      // if error is EAGAIN or EWOULDBLOCK, it means that there is no more data
      // to read right now
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        logs('D', "No more data to read from Client %s", NULL, client->ip);
        return 0;
      } else {
        // a real error occured, so handle it
        return -1;
      }
    }

    // if we got this far, this means that bytes_read > 0
    // so add the new data to client's in_buffer
    // check for buffer overflow first before copying data
    // if new data exceeds fixed size buffer then treat as error
    if (client->in_buffer_len + bytes_read >= MAX_BUFFER_SIZE) {
      return -1;
    }

    // copy new data from temp buffer into client main buffer
    memcpy(client->in_buffer + client->in_buffer_len, temp_buffer, bytes_read);
    client->in_buffer_len += bytes_read;

    // null terminate for easier parsing late
    client->in_buffer[client->in_buffer_len] = '\0';

    printf("%s", client->in_buffer);
  }
}

// ##############################################################################
// ## response handling
// ##############################################################################

void send_headers(client_t *client, int epoll_fd) {
  // TODO: send headers separately
}

// this function sends the headers and the file / body
void write_client_response(client_t *client, int epoll_fd) {
  // first send the headers
  // then determine if the body is a file or a string
  // if it's a file, send it using serve_file()
  // if it's a string, send it using write_response()
}

// function for writing normal full response to client
int write_response(int sock, const char *response_msg) {
  // Minimal HTTP/1.1 response
  const char *template = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n"
                         "%s";

  char buffer[MAX_BUFFER_SIZE];
  int len = snprintf(buffer, sizeof(buffer), template, strlen(response_msg),
                     response_msg);

  if (len < 0 || len >= (int)sizeof(buffer)) {
    logs('E', "Failed to format HTTP response.",
         "write_response(): snprintf() failed or truncated.");
    return -1;
  }

  int bytes_written = write(sock, buffer, len);
  if (bytes_written < 0) {
    logs('E', "Failed to write response to client.",
         "write_response(): write() failed.");
    return -1;
  }
  return bytes_written;
}

void serve_file(client_t *client, int epoll_fd) {}

// ##############################################################################
// ## server process handling
// ##############################################################################

void run_worker(int *listen_sockets, int num_sockets) {
  // --- Start epoll setup ---
  int epoll_fd;
  struct epoll_event event;
  struct epoll_event events[MAX_EVENTS];
  int num_events;

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    logs('E', "Worker failed to create epoll instance.",
         "run_worker(): epoll_create1() failed.");
    exits();
  }

  // add the listening sockets to the epoll instance
  for (int i = 0; i < num_sockets; i++) {
    event.events = EPOLLIN;
    event.data.fd = listen_sockets[i];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sockets[i], &event) == -1) {
      logs('E', "Worker failed to register listening socket.",
           "run_worker(): epoll_ctl() failed.");
      close(epoll_fd);
      exits();
    }
  }
  // --- End epoll setup ---

  client_map = g_hash_table_new(g_direct_hash, g_direct_equal);
  if (client_map == NULL) {
    logs('E', "Worker failed to create client_map.",
         "run_worker(): g_hash_table_new() failed.");
    exits();
  }

  while (1) {
    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events == -1) {
      if (errno == EINTR) {
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
        handle_new_connection(current_fd, epoll_fd);
      } else {
        // get client from client map
        client_t *client = (client_t *)g_hash_table_lookup(
            client_map, GINT_TO_POINTER(current_fd));

        if (events[i].events & EPOLLIN) {
          // it's a read event
          if (read_client_request(client) == 0) {
            // read returns 0, which means success
            // transition epoll event to EPOLLOUT and set state to WRITING
            transition_state(client, epoll_fd, WRITING);
          } else {
            // read returns -1, which means error, so close connection
            transition_state(client, epoll_fd, CLOSING);
          }

        } else if (events[i].events & EPOLLOUT) {
          // it's a write event
          if (write_response(client->fd, "Hello world") == 0) {
            // write returns 0, which means success
            // transition epoll event to EPOLLIN and set state to READING
            transition_state(client, epoll_fd, READING);
          } else {
            // write returns -1, which means error, so close connection
            transition_state(client, epoll_fd, CLOSING);
          }

          // TODO: handle keep-alive
        } else {
          logs('E', "Unexpected event on socket %d", NULL, current_fd);
          close_connection(client, epoll_fd);
        }
      }
    }
  }
}

void init_sockets(int *listen_sockets) {
  server_config *servers = global_config->http->servers;

  for (int i = 0; i < global_config->http->num_servers; i++) {
    listen_sockets[i] = setup_listening_socket(servers[i].listen_port);
    logs('I', "%s listening on port %d.", NULL, servers[i].server_name,
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
      logs('I', "Worker process %d started", NULL, i);
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
    if (!servers[i].server_name) {
      char default_name[32];
      snprintf(default_name, sizeof(default_name), "server%d", i + 1);
      servers[i].server_name = strdup(default_name);
      logs('W', "No name configured for server %d. Setting to default: %s",
           NULL, i + 1, default_name);
    }

    if (!servers[i].listen_port) {
      logs('E', "No port configured for server %s", NULL,
           servers[i].server_name);
      exits();
    }
    if (servers[i].listen_port < 1024) {
      logs('E',
           "Invalid port number. Configure a different port for %s (>1024).",
           NULL, servers[i].server_name);
      exits();
    }
  }
}

int main() {
  logs('I', "Starting server...", NULL);

  // init config
  init_config();

  // TODO: parse config (leave for now)
  // parse_config();

  // check if config is valid
  check_valid_config();

  // setup listening sockets
  int listen_sockets[global_config->http->num_servers];

  // fill socket array with server block sockets
  init_sockets(listen_sockets);

  // fork worker processes
  fork_workers(listen_sockets);

  // clean up server resources
  server_cleanup(listen_sockets);

  // TODO: free_config here when it's implemented
  // free_config();

  return 0;
}
