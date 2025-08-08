#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "minheap_util.h"
#include "parser.h"
#include "server.h"

#define VERBOSE_MODE 1

// Global counter for active client connections
static int active_clients_count = 0;

// Global flag to control the main server loop
volatile int running = 1;

// Global hash table to store client states
GHashTable *client_states_map = NULL;

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state) {
  if (client_state == NULL) {
    return;
  }

  int current_fd = client_state->fd;
  // printf("INFO: Initiating closure for client socket %d.\n", current_fd);

  if (client_state->timeout_heap_index != -1) {
    remove_timeout_by_fd(current_fd);
  }

  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 &&
      errno != ENOENT) {
    fprintf(stderr, "ERROR: An internal server error occurred while closing a "
                    "client connection.\n");
#ifdef VERBOSE_MODE
    perror("REASON: epoll_ctl: DEL client socket failed");
#endif
  }

  close(current_fd);

  g_hash_table_remove(client_states_map, GINT_TO_POINTER(current_fd));

  // printf("INFO: Client socket %d fully closed and removed.\n", current_fd);

  active_clients_count--;
  // printf("INFO: Active clients: %d\n", active_clients_count);
}

// --- The state transition function ---
void transition_state(int epoll_fd, client_state_t *client,
                      client_state_enum_t new_state) {
  if (client->state == new_state) {
    return;
  }

  // printf("INFO: Client %d transitioning from state %d to state %d.\n",
  // client->fd, client->state, new_state);

  // Update the client's timeout on every state change to a reading state
  if (new_state == STATE_READING_REQUEST || new_state == STATE_READING_BODY) {
    time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
    update_timeout(client->fd, expires_at);
  } else {
    // For other states, remove the timeout as it's not idle
    if (client->timeout_heap_index != -1) {
      remove_timeout_by_fd(client->fd);
    }
  }

  client->state = new_state;
  struct epoll_event new_event;
  new_event.data.ptr = client;

  switch (new_state) {
  case STATE_READING_REQUEST:
    // Reset buffers and other state variables
    client->in_buffer_len = 0;
    client->out_buffer_len = 0;
    client->out_buffer_sent = 0;
    client->body_received = 0;
    client->content_length = 0;

    new_event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
      fprintf(stderr,
              "ERROR: An internal server error occurred while managing client "
              "%d.\n",
              client->fd);
#ifdef VERBOSE_MODE
      perror("REASON: epoll_ctl failed to modify epoll interest");
#endif
      close_client_connection(epoll_fd, client);
    }
    break;

  case STATE_READING_BODY:
    new_event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
      fprintf(stderr,
              "ERROR: An internal server error occurred while managing client "
              "%d.\n",
              client->fd);
#ifdef VERBOSE_MODE
      perror("REASON: epoll_ctl failed to modify epoll interest");
#endif
      close_client_connection(epoll_fd, client);
    }
    break;

  case STATE_WRITING_RESPONSE:
    new_event.events = EPOLLOUT | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
      fprintf(stderr,
              "ERROR: An internal server error occurred while managing client "
              "%d.\n",
              client->fd);
#ifdef VERBOSE_MODE
      perror("REASON: epoll_ctl failed to modify epoll interest");
#endif
      close_client_connection(epoll_fd, client);
    }
    break;

  case STATE_CLOSED:
    close_client_connection(epoll_fd, client);
    break;

  default:
    break;
  }
}

// Function to set a file descriptor (like a socket) to non-blocking mode.
int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    fprintf(stderr,
            "ERROR: An internal server error occurred during socket setup.\n");
#ifdef VERBOSE_MODE
    perror("REASON: fcntl F_GETFL failed");
#endif
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    fprintf(stderr,
            "ERROR: An internal server error occurred during socket setup.\n");
#ifdef VERBOSE_MODE
    perror("REASON: fcntl F_SETFL O_NONBLOCK failed");
#endif
    return -1;
  }
  return 0;
}

// Function to set up the listening socket
int setup_listening_socket(int port) {
  int listen_sock;
  struct sockaddr_in server_addr;
  int opt = 1;

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == -1) {
    fprintf(stderr, "ERROR: Failed to create a listening socket. The server "
                    "cannot start.\n");
#ifdef VERBOSE_MODE
    perror("REASON: socket creation failed");
#endif
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    fprintf(stderr, "ERROR: Failed to configure socket options. The server "
                    "cannot start.\n");
#ifdef VERBOSE_MODE
    perror("REASON: setsockopt SO_REUSEADDR failed");
#endif
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    fprintf(stderr,
            "ERROR: Failed to bind the socket to port %d. The port may be in "
            "use.\n",
            port);
#ifdef VERBOSE_MODE
    perror("REASON: bind failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    fprintf(stderr,
            "ERROR: Failed to prepare the socket for incoming connections.\n");
#ifdef VERBOSE_MODE
    perror("REASON: listen failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    fprintf(stderr, "ERROR: Failed to configure the listening socket for "
                    "non-blocking operations.\n");
    // The reason is already printed in set_nonblocking
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}

// Function to handle a new incoming connection on the listening socket.
void handle_new_connection(int listen_sock, int epoll_fd) {
  int conn_sock;
  struct sockaddr_in client_addr;
  socklen_t client_len;
  struct epoll_event event;
  client_state_t *client_state;

  while (1) {
    client_len = sizeof(client_addr);
    conn_sock =
        accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
    if (conn_sock == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        fprintf(stderr,
                "ERROR: The server failed to accept a new connection.\n");
#ifdef VERBOSE_MODE
        perror("REASON: accept failed");
#endif
        break; // Break the while loop and continue the main epoll loop
      }
    }

    if (set_nonblocking(conn_sock) == -1) {
      // The reason is already printed in set_nonblocking
      close(conn_sock);
      continue;
    }

    client_state = (client_state_t *)malloc(sizeof(client_state_t));
    if (client_state == NULL) {
      fprintf(stderr, "ERROR: The server ran out of memory while trying to "
                      "handle a new client.\n");
#ifdef VERBOSE_MODE
      perror("REASON: malloc failed to allocate client state");
#endif
      close(conn_sock);
      continue;
    }
    memset(client_state, 0, sizeof(client_state_t));
    client_state->fd = conn_sock;
    client_state->state = STATE_READING_REQUEST;
    client_state->last_activity_time = time(NULL);
    client_state->timeout_heap_index = -1;

    time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
    add_timeout(conn_sock, expires_at);

    g_hash_table_insert(client_states_map, GINT_TO_POINTER(conn_sock),
                        client_state);

    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = client_state;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
      fprintf(stderr, "ERROR: Failed to add new client %d to the event loop.\n",
              conn_sock);
#ifdef VERBOSE_MODE
      perror("REASON: epoll_ctl failed to add client socket");
#endif
      g_hash_table_remove(client_states_map, GINT_TO_POINTER(conn_sock));
      close(conn_sock);
      continue;
    }

    active_clients_count++;
    printf("INFO: New connection accepted on socket %d. Total active clients: "
           "%d.\n",
           conn_sock, active_clients_count);
  }
}

// Function to handle reading data from a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_read_event(client_state_t *client_state, int epoll_fd) {
  int current_fd = client_state->fd;

  client_state->last_activity_time = time(NULL);
  time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
  update_timeout(current_fd, expires_at);

  // Read loop
  while (1) {
    ssize_t bytes_transferred;
    size_t bytes_to_read;
    char *read_destination;

    if (client_state->state == STATE_READING_REQUEST) {
      bytes_to_read =
          sizeof(client_state->in_buffer) - 1 - client_state->in_buffer_len;
      read_destination = client_state->in_buffer + client_state->in_buffer_len;
    } else if (client_state->state == STATE_READING_BODY) {
      if (client_state->content_length > MAX_BODY_SIZE) {
        create_http_error_response(client_state, 413, "Payload Too Large");
        transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
        return 0;
      }
      bytes_to_read =
          client_state->content_length - client_state->body_received;
      read_destination =
          client_state->body_buffer + client_state->body_received;
    } else {
      break;
    }

    if (bytes_to_read == 0) {
      break;
    }

    bytes_transferred = read(current_fd, read_destination, bytes_to_read);

    // This INFO log is too verbose. It's better to show it only when a full
    // request is received. printf("INFO: Received request from Client %d.\n",
    // current_fd);

    if (bytes_transferred == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        fprintf(stderr,
                "ERROR: An error occurred while reading data from client %d.\n",
                current_fd);
#ifdef VERBOSE_MODE
        perror("REASON: read client socket failed");
#endif
        return 1;
      }
    } else if (bytes_transferred == 0) {
      printf("INFO: Client %d closed connection.\n", current_fd);
      return 1;
    }

    if (client_state->state == STATE_READING_REQUEST) {
      client_state->in_buffer_len += bytes_transferred;
      client_state->in_buffer[client_state->in_buffer_len] = '\0';

      // Check for end of headers with a more efficient character-by-character
      // scan
      char *headers_end = strstr(client_state->in_buffer, "\r\n\r\n");

      if (headers_end) {
        // printf("DEBUG: Received full headers for Client %d.\n", current_fd);
        int parse_success = parse_http_request(client_state);

        if (!parse_success) {
          fprintf(stderr, "ERROR: Malformed HTTP request from client %d.\n",
                  current_fd);
#ifdef VERBOSE_MODE
          fprintf(stderr, "REASON: Failed to parse HTTP headers.\n");
#endif
          transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
        } else {
          if (strcasecmp(client_state->method, "POST") == 0 &&
              client_state->content_length > 0) {
            if (client_state->body_received >= client_state->content_length) {
// printf("DEBUG: Full body received on first read, transitioning
// to WRITING_RESPONSE for Client %d.\n", current_fd);
#ifdef VERBOSE_MODE
              printf("DEBUG: Full body for Client %d: %s\n", current_fd,
                     client_state->body_buffer);
#endif
              create_http_response(client_state);
              transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
            } else {
              // printf("DEBUG: Partial body received on first read,
              // transitioning to READING_BODY for Client %d.\n", current_fd);
              transition_state(epoll_fd, client_state, STATE_READING_BODY);
            }
          } else {
            create_http_response(client_state);
            transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
          }
        }
      }
    } else if (client_state->state == STATE_READING_BODY) {
      client_state->body_received += bytes_transferred;
      if (client_state->body_received >= client_state->content_length) {
        client_state->body_buffer[client_state->content_length] = '\0';
// printf("DEBUG: Full body finally received, transitioning to WRITING_RESPONSE
// for Client %d\n", current_fd);
#ifdef VERBOSE_MODE
        printf("DEBUG: Full body for Client %d: %s\n", current_fd,
               client_state->body_buffer);
#endif
        create_http_response(client_state);
        transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
      }
    }
  }
  return 0;
}

// Function to handle writing data to a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_write_event(client_state_t *client_state, int epoll_fd) {
  int current_fd = client_state->fd;
  ssize_t bytes_transferred;

  if (client_state->state != STATE_WRITING_RESPONSE) {
    // printf("WARNING: Client %d received EPOLLOUT but not in WRITING_RESPONSE
    // state. Closing.\n", current_fd);
    transition_state(epoll_fd, client_state, STATE_CLOSED);
    return 0;
  }

  size_t remaining_to_send =
      client_state->out_buffer_len - client_state->out_buffer_sent;

  if (remaining_to_send > 0) {
    bytes_transferred = write(
        current_fd, client_state->out_buffer + client_state->out_buffer_sent,
        remaining_to_send);

    if (bytes_transferred == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      } else {
        fprintf(
            stderr,
            "ERROR: An error occurred while sending a response to client %d.\n",
            current_fd);
#ifdef VERBOSE_MODE
        perror("REASON: write client socket failed");
#endif
        transition_state(epoll_fd, client_state, STATE_CLOSED);
        return 0;
      }
    } else {
      client_state->out_buffer_sent += bytes_transferred;

      if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
        printf("INFO: Response data sent to Client %d.\n", current_fd);
        if (client_state->keep_alive) {
          transition_state(epoll_fd, client_state, STATE_READING_REQUEST);
        } else {
          printf("INFO: Closing connection for Client %d (keep-alive not "
                 "requested).\n",
                 current_fd);
          transition_state(epoll_fd, client_state, STATE_CLOSED);
        }
      }
      return 0;
    }
  } else {
    // printf("DEBUG: EPOLLOUT on client %d but nothing to send. Closing.\n",
    // current_fd);
    transition_state(epoll_fd, client_state, STATE_CLOSED);
    return 0;
  }
}

// Function to handle events on an existing client socket.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr) {
  client_state_t *client_state = (client_state_t *)event_ptr->data.ptr;
  if (client_state == NULL) {
    fprintf(stderr, "ERROR: An internal server error occurred due to an "
                    "invalid client state.\n");
#ifdef VERBOSE_MODE
    fprintf(stderr, "REASON: handle_client_event: client_state is NULL.\n");
#endif
    return;
  }
  int current_fd = client_state->fd;
  uint32_t event_flags = event_ptr->events;

  if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    printf("INFO: Client %d disconnected or an error occurred.\n", current_fd);
    transition_state(epoll_fd, client_state, STATE_CLOSED);
  } else if (event_flags & EPOLLIN) {
    if (handle_read_event(client_state, epoll_fd)) {
      transition_state(epoll_fd, client_state, STATE_CLOSED);
    }
  } else if (event_flags & EPOLLOUT) {
    handle_write_event(client_state, epoll_fd);
  }
}

// Signal handler function for SIGINT (Ctrl+C)
void handle_sigint() {
  // printf("\nINFO: SIGINT received. Shutting down server gracefully...\n");
  running = 0;
}

// Add this function to your server.c file
void cleanup_client_state_on_destroy(gpointer data) {
  client_state_t *client_state = (client_state_t *)data;

  if (client_state->body_buffer != NULL) {
    free(client_state->body_buffer);
    client_state->body_buffer = NULL;
  }

  free(client_state);
}

int main() {
  int listen_sock;
  int epoll_fd;
  struct epoll_event event;
  struct epoll_event events[MAX_EVENTS];
  int num_events;
  int i;

  client_states_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                            cleanup_client_state_on_destroy);
  init_min_heap();

  printf("INFO: Server listening on port %d.\n", PORT);

  if (signal(SIGINT, handle_sigint) == SIG_ERR) {
    fprintf(stderr, "ERROR: The server failed to set up an exit handler.\n");
#ifdef VERBOSE_MODE
    fprintf(stderr, "REASON: signal registration for SIGINT failed.\n");
#endif
    exit(EXIT_FAILURE);
  }

  listen_sock = setup_listening_socket(PORT);
  if (listen_sock == -1) {
    fprintf(stderr, "ERROR: The server failed to start.\n");
#ifdef VERBOSE_MODE
    fprintf(stderr, "REASON: Failed to setup listening socket.\n");
#endif
    exit(EXIT_FAILURE);
  }

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    fprintf(stderr,
            "ERROR: The server failed to create a necessary event handler.\n");
#ifdef VERBOSE_MODE
    fprintf(stderr, "REASON: epoll_create1 failed.\n");
#endif
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  event.events = EPOLLIN | EPOLLET;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
    fprintf(stderr, "ERROR: The server failed to register its main socket.\n");
#ifdef VERBOSE_MODE
    fprintf(stderr, "REASON: epoll_ctl failed for listening socket.\n");
#endif
    close(listen_sock);
    close(epoll_fd);
    exit(EXIT_FAILURE);
  }

  while (running) {
    long epoll_timeout_ms = get_next_timeout_ms();

    if (heap_size > 0) {
      // printf("DEBUG: Next epoll_wait timeout: %ldms. Heap size: %zu.\n",
      // epoll_timeout_ms, heap_size);
    }

    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_timeout_ms);

    if (num_events == -1) {
      if (errno == EINTR) {
        if (!running) {
          break;
        }
        continue;
      }
      fprintf(
          stderr,
          "ERROR: A critical system call failed in the main server loop.\n");
#ifdef VERBOSE_MODE
      fprintf(stderr, "REASON: epoll_wait failed.\n");
#endif
      running = 0;
      continue;
    }

    if (num_events > 0) {
      // printf("DEBUG: epoll_wait returned %d events.\n", num_events);
    }

    for (i = 0; i < num_events; i++) {
      if (events[i].data.fd == listen_sock) {
        handle_new_connection(listen_sock, epoll_fd);
      } else {
        handle_client_event(epoll_fd, &events[i]);
      }
    }

    while (heap_size > 0) {
      time_t current_time = time(NULL);
      if (timeout_heap[0].expires > current_time) {
        break;
      }

      int expired_fd = timeout_heap[0].fd;
      client_state_t *client_state =
          g_hash_table_lookup(client_states_map, GINT_TO_POINTER(expired_fd));

      if (client_state != NULL) {
        printf("INFO: Client %d timed out. Closing connection.\n", expired_fd);
        close_client_connection(epoll_fd, client_state);
      } else {
#ifdef VERBOSE_MODE
// printf("WARNING: Expired FD %d not found in hash table. Likely already
// closed. Removing from heap.\n", expired_fd);
#endif
        remove_min_timeout();
      }
    }
  }

  printf("INFO: Cleaning up resources. Server shutting down.\n");
  close(listen_sock);
  g_hash_table_destroy(client_states_map);
  close(epoll_fd);
  free(timeout_heap);

  // printf("INFO: Server shutdown complete.\n");

  return 0;
}
