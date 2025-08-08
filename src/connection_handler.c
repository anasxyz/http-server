#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
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

int active_clients_count = 0;

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

