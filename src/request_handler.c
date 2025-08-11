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

#include "minheap_util.h"
#include "parser.h"
#include "request_handler.h"
#include "server.h"
#include "state_handler.h"
#include "ssl.h"

// Function to handle reading data from a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_read_event(client_state_t *client_state, int epoll_fd,
                      GHashTable *client_states_map,
                      int *active_connections_ptr) {
  int current_fd = client_state->fd;

  client_state->last_activity_time = time(NULL);
  time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
  update_timeout(current_fd, expires_at, client_states_map);

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
        transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                         client_states_map, active_connections_ptr);
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

    // --- SSL: Use SSL_read_wrapper instead of read ---
    bytes_transferred =
        ssl_read_wrapper(client_state->ssl, read_destination, bytes_to_read);
    // --- End SSL ---

    if (bytes_transferred <= 0) {
      int err = SSL_get_error(client_state->ssl, bytes_transferred);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        break; // No more data to read for now
      } else if (err == SSL_ERROR_SYSCALL) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
      }

      // Handle disconnection or fatal error
      if (bytes_transferred == 0) {
        printf("INFO: Client %d closed connection.\n", current_fd);
      } else {
        fprintf(stderr, "ERROR: SSL_read error on client %d.\n", current_fd);
        ERR_print_errors_fp(stderr);
      }
      return 1; // Return 1 to signal close
    }

    if (client_state->state == STATE_READING_REQUEST) {
      client_state->in_buffer_len += bytes_transferred;
      client_state->in_buffer[client_state->in_buffer_len] = '\0';

      char *headers_end = strstr(client_state->in_buffer, "\r\n\r\n");

      if (headers_end) {
        int parse_success = parse_http_request(client_state);

        if (!parse_success) {
          fprintf(stderr, "ERROR: Failed to parse HTTP headers.\n");
#ifdef VERBOSE_MODE
          fprintf(stderr, "REASON: Malformed HTTP request from client %d.\n",
                  current_fd);
#endif
          transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                           client_states_map, active_connections_ptr);
        } else {
          if (strcasecmp(client_state->method, "POST") == 0 &&
              client_state->content_length > 0) {
            if (client_state->body_received >= client_state->content_length) {
              create_http_response(client_state);
              transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                               client_states_map, active_connections_ptr);
            } else {
              transition_state(epoll_fd, client_state, STATE_READING_BODY,
                               client_states_map, active_connections_ptr);
            }
          } else {
            create_http_response(client_state);
            transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                             client_states_map, active_connections_ptr);
          }
        }
      }
    } else if (client_state->state == STATE_READING_BODY) {
      client_state->body_received += bytes_transferred;
      if (client_state->body_received >= client_state->content_length) {
        client_state->body_buffer[client_state->content_length] = '\0';
#ifdef VERBOSE_MODE
        printf("DEBUG: Full body for Client %d: %s\n", current_fd,
               client_state->body_buffer);
#endif
        create_http_response(client_state);
        transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                         client_states_map, active_connections_ptr);
      }
    }
  }
  return 0;
}
