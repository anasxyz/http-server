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

#include "connection_handler.h"
#include "minheap_util.h"
#include "server.h"
#include "utils.h"

volatile int running = 1;

// Global hash table to store client states
GHashTable *client_states_map = NULL;

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


// Add this function to your server.c file
void cleanup_client_state_on_destroy(gpointer data) {
  client_state_t *client_state = (client_state_t *)data;

  if (client_state->body_buffer != NULL) {
    free(client_state->body_buffer);
    client_state->body_buffer = NULL;
  }

  free(client_state);
}


// Signal handler function for SIGINT (Ctrl+C)
void handle_sigint() {
  // printf("\nINFO: SIGINT received. Shutting down server gracefully...\n");
  running = 0;
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


