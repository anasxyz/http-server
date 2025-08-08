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

#include "minheap_util.h"
#include "parser.h"
#include "request_handler.h"
#include "server.h"
#include "state_handler.h"

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
              // printf("DEBUG: Full body received on first read, transitioning
              // to WRITING_RESPONSE for Client %d.\n", current_fd);
              // printf("DEBUG: Full body for Client %d: %s\n", current_fd,
              // client_state->body_buffer);
              create_http_response(client_state);
              transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE,
                               client_states_map, active_connections_ptr);
            } else {
              // printf("DEBUG: Partial body received on first read,
              // transitioning to READING_BODY for Client %d.\n", current_fd);
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
// printf("DEBUG: Full body finally received, transitioning to WRITING_RESPONSE
// for Client %d\n", current_fd);
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
