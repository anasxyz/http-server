#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdbool.h>
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
#include "request_handler.h"
#include "response_handler.h"
#include "server.h"
#include "state_handler.h"
#include "utils.h"

// Function to handle a new incoming connection on the listening socket.
void handle_new_connection(int listen_sock, int epoll_fd,
                           GHashTable *client_states_map,
                           int *active_connections_ptr) {
  int conn_sock;
  struct sockaddr_in client_addr;
  socklen_t client_len;
  struct epoll_event event;
  client_state_t *client_state;

  static bool is_max_connections_logged = false;

  // Check if the worker has reached its connection limit before accepting.
  // The check is now done once per call.
  if (*active_connections_ptr >= MAX_CONNECTIONS_PER_WORKER) {
    if (VERBOSE_MODE && !is_max_connections_logged) {
      printf("WARNING: Worker %d has reached max connections (%d). Not "
             "accepting new ones.\n",
             getpid(), *active_connections_ptr);
      is_max_connections_logged = true; // Set the flag to true
    }
    return; // Return immediately, don't try to accept.
  }

  // if not at max capacity, reset the flag
  is_max_connections_logged = false;

  client_len = sizeof(client_addr);
  conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
  if (conn_sock == -1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      fprintf(stderr, "ERROR: The server failed to accept a new connection.\n");
#ifdef VERBOSE_MODE
      perror("REASON: accept failed");
#endif
    }
    return; // No more connections to accept for this event.
  }

  if (set_nonblocking(conn_sock) == -1) {
    close(conn_sock);
    return;
  }

  client_state = (client_state_t *)malloc(sizeof(client_state_t));
  if (client_state == NULL) {
    fprintf(stderr, "ERROR: The server ran out of memory while trying to "
                    "handle a new client.\n");
#ifdef VERBOSE_MODE
    perror("REASON: malloc failed to allocate client state");
#endif
    close(conn_sock);
    return;
  }

  memset(client_state, 0, sizeof(client_state_t));
  client_state->fd = conn_sock;
  client_state->state = STATE_READING_REQUEST;
  client_state->last_activity_time = time(NULL);
  client_state->timeout_heap_index = -1;

  // Increment the active connections count for this worker.
  (*active_connections_ptr)++;
  // ...and the shared atomic counter for the server total.
  atomic_fetch_add(total_connections, 1);

  time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
  add_timeout(conn_sock, expires_at, client_states_map);

  g_hash_table_insert(client_states_map, GINT_TO_POINTER(conn_sock),
                      client_state);

  event.events = EPOLLIN;
  event.data.ptr = client_state;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
    fprintf(stderr, "ERROR: Failed to add new client %d to the event loop.\n",
            conn_sock);
#ifdef VERBOSE_MODE
    perror("REASON: epoll_ctl failed to add client socket");
#endif
    // Decrement the counter since we failed to add the client.
    (*active_connections_ptr)--;
    g_hash_table_remove(client_states_map, GINT_TO_POINTER(conn_sock));
    close(conn_sock);
    return;
  }

  printf("INFO: Worker %d accepted new connection on socket %d. Worker "
         "active: %d. Total: %d.\n",
         getpid(), conn_sock, *active_connections_ptr,
         atomic_load(total_connections));
}

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state,
                             GHashTable *client_states_map,
                             int *active_connections_ptr) {
  if (client_state == NULL) {
    return;
  }

  int current_fd = client_state->fd;

  // A crucial safeguard: check if the client state still exists in the hash
  // table. If it doesn't, this function has already been called for this
  // client.
  if (!g_hash_table_contains(client_states_map, GINT_TO_POINTER(current_fd))) {
    return; // The client was already closed, so do nothing.
  }

  // Unconditionally remove the timeout from the heap.
  if (client_state->timeout_heap_index != -1) {
    remove_timeout_by_fd(current_fd, client_states_map);
  }

  // Remove the socket from the epoll instance.
  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 &&
      errno != ENOENT) {
    perror("REASON: epoll_ctl: DEL client socket failed");
  }

  // Close the file descriptor.
  close(current_fd);

  // Remove the client from the hash table. This will free the memory.
  g_hash_table_remove(client_states_map, GINT_TO_POINTER(current_fd));

  // Decrement the per-worker connection count.
  (*active_connections_ptr)--;

  // Decrement the shared atomic counter for the server total.
  atomic_fetch_sub(total_connections, 1);

  printf("INFO: Client socket %d fully closed. Active connections: %d.\n",
         current_fd, atomic_load(total_connections));
}

// Function to handle events on an existing client socket.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr,
                         GHashTable *client_states_map,
                         int *active_connections_ptr) {
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
    // Pass the new pointer to transition_state
    transition_state(epoll_fd, client_state, STATE_CLOSED, client_states_map,
                     active_connections_ptr);
  } else if (event_flags & EPOLLIN) {
    // Pass the new pointer to handle_read_event
    if (handle_read_event(client_state, epoll_fd, client_states_map,
                          active_connections_ptr)) {
      // If the read handler returns 1 (indicating a close is needed), pass the
      // pointer here as well.
      transition_state(epoll_fd, client_state, STATE_CLOSED, client_states_map,
                       active_connections_ptr);
    }
  } else if (event_flags & EPOLLOUT) {
    // Pass the new pointer to handle_write_event
    handle_write_event(client_state, epoll_fd, client_states_map,
                       active_connections_ptr);
  }
}
