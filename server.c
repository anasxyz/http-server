#include <errno.h> // For errno and EWOULDBLOCK/EAGAIN
#include <fcntl.h> // For fcntl (file control) to set non-blocking mode
#include <glib.h>
#include <netinet/in.h> // For sockaddr_in structure
#include <signal.h>     // For signal handling (SIGINT)
#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit, malloc, free
#include <string.h>     // For string manipulation like memset, strlen, strstr
#include <sys/epoll.h>  // For epoll functions
#include <sys/select.h> // For FD_SETSIZE, a common max FD limit
#include <sys/socket.h> // For socket, bind, listen, accept functions
#include <time.h>       // For time() function to get current time
#include <unistd.h>     // For close function, read, write

#include "minheap_util.h"
#include "parser.h"
#include "server.h"

// Global counter for active client connections
static int active_clients_count = 0;

// Global flag to control the main server loop
volatile int running = 1;

// Global hash table to store client states
GHashTable *client_states_map = NULL;

// --- Helper Functions ---

// Function to print the current state of the timeout min-heap
void print_heap_state() {
  if (heap_size == 0) {
    printf("DEBUG: Timeout heap is empty.\n");
  } else {
    printf("DEBUG: Timeout heap size: %zu. Next timeout for FD %d at %ld.\n",
           heap_size, timeout_heap[0].fd, timeout_heap[0].expires);
  }
}

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state) {
  if (client_state == NULL) {
    return;
  }

  int current_fd = client_state->fd;
  printf("INFO: Initiating closure for client socket %d.\n", current_fd);

  if (client_state->timeout_heap_index != -1) {
    remove_timeout_by_fd(current_fd);
  }

  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 &&
      errno != ENOENT) {
    perror("epoll_ctl: DEL client socket failed");
  }

  close(current_fd);

  g_hash_table_remove(client_states_map, GINT_TO_POINTER(current_fd));

  printf("INFO: Client socket %d fully closed and removed. 🚪\n", current_fd);

  active_clients_count--;
  printf("\n===============================================\n");
  printf("INFO: Active clients: %d\n", active_clients_count);
  printf("===============================================\n");
}

// --- The state transition function ---
void transition_state(int epoll_fd, client_state_t *client,
                      client_state_enum_t new_state) {
  if (client->state == new_state) {
    return;
  }

  printf("INFO: Client %d transitioning from state %d to state %d.\n",
         client->fd, client->state, new_state);

  // Always remove the existing timeout before a state change
  if (client->timeout_heap_index != -1) {
    remove_timeout_by_fd(client->fd);
  }

  client->state = new_state;
  struct epoll_event new_event;
  new_event.data.ptr = client;

  switch (new_state) {
  case STATE_READING_REQUEST:
    client->in_buffer_len = 0;
    client->out_buffer_len = 0;
    client->out_buffer_sent = 0;
    client->body_received = 0;
    client->content_length = 0;

    time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
    add_timeout(client->fd, expires_at);

    new_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event);
    break;

  case STATE_READING_BODY:
    time_t body_expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
    add_timeout(client->fd, body_expires_at);

    new_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event);
    break;

  case STATE_WRITING_RESPONSE:
    new_event.events = EPOLLOUT | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event);
    break;

  case STATE_IDLE:
    printf(
        "INFO: Client %d is now IDLE, awaiting next request or timeout. ⏳\n",
        client->fd);
    break;

  case STATE_CLOSED:
    close_client_connection(epoll_fd, client);
    break;
  }
}

// Function to set a file descriptor (like a socket) to non-blocking mode.
int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL failed");
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL O_NONBLOCK failed");
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
    perror("socket creation failed");
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    perror("setsockopt SO_REUSEADDR failed");
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    perror("bind failed");
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    perror("listen failed");
    close(listen_sock);
    return -1;
  }

  printf("INFO: Server is listening for connections on port %d...👂\n", port);

  if (set_nonblocking(listen_sock) == -1) {
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
        perror("accept failed");
        exit(EXIT_FAILURE);
      }
    }

    if (set_nonblocking(conn_sock) == -1) {
      close(conn_sock);
      continue;
    }

    client_state = (client_state_t *)malloc(sizeof(client_state_t));
    if (client_state == NULL) {
      perror("malloc client_state failed");
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
      perror("epoll_ctl: adding conn_sock failed");
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
  remove_timeout_by_fd(current_fd);
  time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
  add_timeout(current_fd, expires_at);

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

    if (bytes_transferred == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("read client socket failed");
        return 1;
      }
    } else if (bytes_transferred == 0) {
      printf("INFO: Client socket %d closed connection during read. 👋\n",
             current_fd);
      return 1;
    }

    if (client_state->state == STATE_READING_REQUEST) {
      client_state->in_buffer_len += bytes_transferred;
      client_state->in_buffer[client_state->in_buffer_len] = '\0';
      if (strstr(client_state->in_buffer, "\r\n\r\n")) {
        printf("DEBUG: Received full headers for client %d.\n", current_fd);
        int parse_success = parse_http_request(client_state);

        if (!parse_success) {
          transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
        } else {
          if (strcasecmp(client_state->method, "POST") == 0 &&
              client_state->content_length > 0) {
            if (client_state->body_received >= client_state->content_length) {
              printf("DEBUG: Full POST body received. Preparing response for "
                     "client %d.\n",
                     current_fd);
              create_http_response(client_state);
              transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
            } else {
              printf("DEBUG: Partial POST body received, transitioning to "
                     "READING_BODY for client %d.\n",
                     current_fd);
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
        printf("DEBUG: Full body received for client %d. Total size: %zu.\n",
               current_fd, client_state->content_length);
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
    printf("WARNING: Client %d received EPOLLOUT but not in WRITING_RESPONSE "
           "state. Closing.\n",
           current_fd);
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
        // Send buffer full, we'll get another EPOLLOUT event
        return 0;
      } else {
        perror("write client socket failed");
        transition_state(epoll_fd, client_state, STATE_CLOSED);
        return 0;
      }
    } else if (bytes_transferred == 0) {
      printf("WARNING: Client socket %d closed connection during write.\n",
             current_fd);
      transition_state(epoll_fd, client_state, STATE_CLOSED);
      return 0;
    } else {
      client_state->out_buffer_sent += bytes_transferred;

      if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
        printf("DEBUG: All response data sent to client %d. ✅\n", current_fd);
        if (client_state->keep_alive) {
          transition_state(epoll_fd, client_state, STATE_READING_REQUEST);
        } else {
          printf("INFO: Closing connection %d (Keep-Alive not requested).\n",
                 current_fd);
          transition_state(epoll_fd, client_state, STATE_CLOSED);
        }
      }
      return 0;
    }
  } else {
    printf("DEBUG: EPOLLOUT on client %d but nothing to send. Closing.\n",
           current_fd);
    transition_state(epoll_fd, client_state, STATE_CLOSED);
    return 0;
  }
}

// Function to handle events on an existing client socket.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr) {
  client_state_t *client_state = (client_state_t *)event_ptr->data.ptr;
  if (client_state == NULL) {
    fprintf(stderr, "handle_client_event: client_state is NULL. ❌\n");
    return;
  }
  int current_fd = client_state->fd;
  uint32_t event_flags = event_ptr->events;

  if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    printf("INFO: Client socket %d disconnected or error occurred. 💀\n",
           current_fd);
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
void handle_sigint(int sig) {
  printf("\nINFO: SIGINT received. Shutting down server gracefully...👋\n");
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

// --- Main Function ---
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

  printf("INFO: Starting simple epoll server on port %d. 🚀\n", PORT);

  if (signal(SIGINT, handle_sigint) == SIG_ERR) {
    perror("signal registration failed");
    exit(EXIT_FAILURE);
  }

  listen_sock = setup_listening_socket(PORT);
  if (listen_sock == -1) {
    exit(EXIT_FAILURE);
  }

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1 failed");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  event.events = EPOLLIN | EPOLLET;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
    perror("epoll_ctl: adding listen_sock failed");
    close(listen_sock);
    close(epoll_fd);
    exit(EXIT_FAILURE);
  }

  while (running) {
    long epoll_timeout_ms = get_next_timeout_ms();

    if (heap_size > 0) {
      printf("DEBUG: Next epoll_wait timeout: %ldms. Heap size: %zu.\n",
             epoll_timeout_ms, heap_size);
    }

    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_timeout_ms);

    if (num_events == -1) {
      if (errno == EINTR) {
        if (!running) {
          break;
        }
        continue;
      }
      perror("epoll_wait failed");
      running = 0;
      continue;
    }

    if (num_events > 0) {
      printf("DEBUG: epoll_wait returned %d events.\n", num_events);
    }

    for (i = 0; i < num_events; i++) {
      if (events[i].data.fd == listen_sock) {
        handle_new_connection(listen_sock, epoll_fd);
      } else {
        handle_client_event(epoll_fd, &events[i]);
      }
    }

    time_t current_time;
    while (heap_size > 0) {
      current_time = time(NULL);
      if (timeout_heap[0].expires > current_time) {
        break;
      }

      int expired_fd = timeout_heap[0].fd;
      client_state_t *client_state =
          g_hash_table_lookup(client_states_map, GINT_TO_POINTER(expired_fd));

      if (client_state != NULL) {
        printf("INFO: Client socket %d timed out. Closing connection. ⏰\n",
               expired_fd);
        close_client_connection(epoll_fd, client_state);
      } else {
        printf("WARNING: Expired FD %d not found in hash table. Likely already "
               "closed. Removing from heap.\n",
               expired_fd);
        remove_min_timeout();
      }
    }
  }

  printf("INFO: Server shutting down. Cleaning up resources...\n");
  close(listen_sock);
  g_hash_table_destroy(client_states_map);
  close(epoll_fd);
  free(timeout_heap);

  printf("INFO: Server shutdown complete. 👋\n");

  return 0;
}
