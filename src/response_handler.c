#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "response_handler.h"
#include "server.h"
#include "state_handler.h"
#include "ssl.h"

// Function to handle writing data to a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_write_event(client_state_t *client_state, int epoll_fd,
                       GHashTable *client_states_map,
                       int *active_connections_ptr) {
  int current_fd = client_state->fd;
  ssize_t bytes_transferred;

  if (client_state->state != STATE_WRITING_RESPONSE) {
    transition_state(epoll_fd, client_state, STATE_CLOSED, client_states_map,
                     active_connections_ptr);
    return 0;
  }

  size_t remaining_to_send =
      client_state->out_buffer_len - client_state->out_buffer_sent;

  if (remaining_to_send > 0) {
    // --- SSL: Use ssl_write_wrapper instead of write ---
    bytes_transferred = ssl_write_wrapper(
        client_state->ssl, client_state->out_buffer + client_state->out_buffer_sent,
        remaining_to_send);
    // --- End SSL ---

    if (bytes_transferred <= 0) {
      int err = SSL_get_error(client_state->ssl, bytes_transferred);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        return 0; // No more data can be written for now
      } else if (err == SSL_ERROR_SYSCALL) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return 0;
        }
      }
      fprintf(stderr, "ERROR: SSL_write error on client %d.\n", current_fd);
      ERR_print_errors_fp(stderr);
      transition_state(epoll_fd, client_state, STATE_CLOSED,
                       client_states_map, active_connections_ptr);
      return 0;
    } else {
      client_state->out_buffer_sent += bytes_transferred;

      if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
        printf("INFO: Response data sent to Client %d.\n", current_fd);
        if (client_state->keep_alive) {
          transition_state(epoll_fd, client_state, STATE_READING_REQUEST,
                           client_states_map, active_connections_ptr);
        } else {
          printf("INFO: Closing connection for Client %d (keep-alive not "
                 "requested).\n",
                 current_fd);
          transition_state(epoll_fd, client_state, STATE_CLOSED,
                           client_states_map, active_connections_ptr);
        }
      }
      return 0;
    }
  } else {
    transition_state(epoll_fd, client_state, STATE_CLOSED, client_states_map,
                     active_connections_ptr);
    return 0;
  }
}
