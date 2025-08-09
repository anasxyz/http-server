#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "connection_handler.h"
#include "minheap_util.h"
#include "server.h"
#include "state_handler.h"

void transition_state(int epoll_fd, client_state_t *client,
                      client_state_enum_t new_state,
                      GHashTable *client_states_map,
                      int *active_connections_ptr) {
    if (client->state == new_state) {
        return;
    }

    // Update the client's timeout on every state change to an "active" state
    if (new_state == STATE_READING_REQUEST || new_state == STATE_READING_BODY ||
        new_state == STATE_TLS_HANDSHAKE) {
        time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
        update_timeout(client->fd, expires_at, client_states_map);
    } else {
        // For other states, like writing or closing, the connection isn't idle.
        if (client->timeout_heap_index != -1) {
            remove_timeout_by_fd(client->fd, client_states_map);
        }
    }

    client->state = new_state;
    struct epoll_event new_event;
    new_event.data.ptr = client;

    switch (new_state) {
        case STATE_TLS_HANDSHAKE:
            // The TLS handshake is a two-way process, so we need to be ready
            // for both read and write events.
            new_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
                fprintf(stderr, "ERROR: An internal server error occurred while "
                                "managing client %d.\n", client->fd);
                close_client_connection(epoll_fd, client, client_states_map,
                                        active_connections_ptr);
            }
            break;

        case STATE_READING_REQUEST:
            // Reset buffers and other state variables for a new HTTP request
            client->in_buffer_len = 0;
            client->out_buffer_len = 0;
            client->out_buffer_sent = 0;
            client->body_received = 0;
            client->content_length = 0;

            // Reading a request only requires read events.
            new_event.events = EPOLLIN | EPOLLET;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
                fprintf(stderr, "ERROR: An internal server error occurred while "
                                "managing client %d.\n", client->fd);
                close_client_connection(epoll_fd, client, client_states_map,
                                        active_connections_ptr);
            }
            break;

        case STATE_READING_BODY:
            // Reading a request body only requires read events.
            new_event.events = EPOLLIN | EPOLLET;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
                fprintf(stderr, "ERROR: An internal server error occurred while "
                                "managing client %d.\n", client->fd);
                close_client_connection(epoll_fd, client, client_states_map,
                                        active_connections_ptr);
            }
            break;

        case STATE_WRITING_RESPONSE:
            // Writing a response only requires write events.
            new_event.events = EPOLLOUT | EPOLLET;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event) == -1) {
                fprintf(stderr, "ERROR: An internal server error occurred while "
                                "managing client %d.\n", client->fd);
                close_client_connection(epoll_fd, client, client_states_map,
                                        active_connections_ptr);
            }
            break;

        case STATE_CLOSED:
            close_client_connection(epoll_fd, client, client_states_map,
                                    active_connections_ptr);
            break;

        default:
            break;
    }
}
