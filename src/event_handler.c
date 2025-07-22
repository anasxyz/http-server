// src/event_handler.c
#include "../include/event_handler.h" // Include its own header first
#include "../include/connection.h"
#include "../include/http.h" // For parse_request, handle_get, handle_post, etc.
#include "../include/config.h" // For proxy functions
#include "../include/proxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h> // For fcntl
#include <errno.h> // For errno

// Define the global epoll file descriptor (initialized here)
int G_epoll_fd = -1;

// Define the global array for active connections (initialized here)
Connection *active_connections[MAX_CONNECTIONS] = {NULL};

// --- Helper functions for managing connections ---
Connection *get_connection_by_fd(int fd) {
    if (fd >= 0 && fd < MAX_CONNECTIONS) {
        return active_connections[fd];
    }
    return NULL;
}

void add_connection(Connection *conn) {
    if (conn->fd >= 0 && conn->fd < MAX_CONNECTIONS) {
        active_connections[conn->fd] = conn;
    } else {
        fprintf(stderr, "Error: FD %d out of bounds for active_connections array.\n", conn->fd);
        connection_destroy(conn);
        close(conn->fd);
    }
}

void remove_connection(Connection *conn) {
    if (conn && conn->fd >= 0 && conn->fd < MAX_CONNECTIONS) {
        epoll_ctl(G_epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL); // Remove from epoll
        close(conn->fd); // Close the socket
        connection_destroy(conn); // Free the connection struct
        active_connections[conn->fd] = NULL; // Clear the slot
    }
}

// --- Main event handling function ---
void handle_epoll_event(struct epoll_event *event, int listen_fd) {
    if (event->data.fd == listen_fd) {
        // New connection on the listening socket
        while (1) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // No more incoming connections to accept
                } else {
                    perror("accept failed");
                    return;
                }
            }

            if (fcntl(client_fd, F_SETFL, O_NONBLOCK) == -1) {
                perror("Failed to set client socket to non-blocking");
                close(client_fd);
                continue;
            }

            Connection *conn = connection_create(client_fd);
            if (!conn) {
                fprintf(stderr, "Failed to create connection struct for fd %d\n", client_fd);
                close(client_fd);
                continue;
            }
            add_connection(conn); // Store it in our active_connections map/array

            struct epoll_event client_event;
            client_event.events = EPOLLIN | EPOLLET; // Edge-triggered read
            client_event.data.ptr = conn; // Associate the Connection struct
            if (epoll_ctl(G_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                perror("Failed to add client socket to epoll");
                remove_connection(conn); // Clean up the connection struct
                continue;
            }
            conn->last_activity_time = time(NULL);
            conn->state = CONN_STATE_READING_REQUEST;
        }
    } else {
        // Event on an existing client socket
        Connection *conn = (Connection *)event->data.ptr;
        if (!conn) {
            fprintf(stderr, "Error: Connection struct not found for fd %d\n", event->data.fd);
            close(event->data.fd);
            return;
        }

        if (event->events & EPOLLERR) {
            fprintf(stderr, "EPOLLERR on fd %d\n", conn->fd);
            remove_connection(conn);
            return;
        }
        if (event->events & EPOLLHUP) {
            printf("Client disconnected from fd %d\n", conn->fd);
            remove_connection(conn);
            return;
        }

        if (event->events & EPOLLIN) {
            conn->last_activity_time = time(NULL);
            ssize_t bytes_read_now;
            while ((bytes_read_now = read(conn->fd, conn->read_buffer + conn->bytes_read,
                                           conn->read_buffer_size - conn->bytes_read -1)) > 0) {
                conn->bytes_read += bytes_read_now;
                conn->read_buffer[conn->bytes_read] = '\0';

                if (strstr(conn->read_buffer, "\r\n\r\n") != NULL) {
                    process_full_request(conn, G_epoll_fd);
                    break;
                }

                if (conn->bytes_read >= conn->read_buffer_size -1) {
                    fprintf(stderr, "Request buffer full for fd %d, request too large or malformed.\n", conn->fd);
                    HttpResponse *response = create_response(413);
                    conn->current_response = response;
                    conn->state = CONN_STATE_SENDING_RESPONSE;
                    remove_connection(conn); // Close connection on error
                    return;
                }
            }

            if (bytes_read_now == 0) {
                printf("Client on fd %d closed connection gracefully.\n", conn->fd);
                remove_connection(conn);
                return;
            } else if (bytes_read_now == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("read failed");
                    remove_connection(conn);
                    return;
                }
            }
        }
        // Add EPOLLOUT handling here if you implement chunked sending
    }
}

// --- Function to check for keep-alive timeouts ---
void check_keep_alive_timeouts() {
    time_t current_time = time(NULL);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        Connection *conn = active_connections[i];
        if (conn && conn->state == CONN_STATE_KEEP_ALIVE) {
            if ((current_time - conn->last_activity_time) > conn->keep_alive_timeout) {
                printf("Keep-alive timeout for fd %d. Closing connection.\n", conn->fd);
                remove_connection(conn);
            }
        }
    }
}

// --- Function to process a full HTTP request ---
void process_full_request(Connection *conn, int epoll_fd) {
    conn->current_request = parse_request(conn->read_buffer);
    if (!conn->current_request) {
        conn->current_response = create_response(400);
        conn->state = CONN_STATE_SENDING_RESPONSE;
        goto send_response;
    }

    int client_wants_keep_alive = 0;
    const char *conn_header = get_header(conn->current_request, "Connection");
    if (conn_header) {
        if (strcmp(conn_header, "keep-alive") == 0) {
            client_wants_keep_alive = 1;
        } else if (strcmp(conn_header, "close") == 0) {
            client_wants_keep_alive = 0;
        }
    } else {
        client_wants_keep_alive = 1; // Default to keep-alive for HTTP/1.1
    }

    Proxy *proxy = find_proxy_for_path(conn->current_request->request_line.path);
    if (proxy) {
        conn->current_response = proxy_request(proxy, conn->read_buffer);
        if (!conn->current_response) {
            conn->current_response = create_response(502);
        }
    } else {
        MethodHandler handlers[] = {
            {"GET", handle_get, NULL},
        };

        int num_handlers = sizeof(handlers) / sizeof(handlers[0]);
        int handler_found = 0;
        for (int i = 0; i < num_handlers; i++) {
            if (strcmp(conn->current_request->request_line.method, handlers[i].method) == 0) {
                conn->current_response = handlers[i].handler(conn->current_request, handlers[i].context);
                handler_found = 1;
                break;
            }
        }
        if (!handler_found) {
            conn->current_response = create_response(405);
            set_header(conn->current_response, "Allow", "GET, POST");
        }
        if (!conn->current_response) {
            conn->current_response = create_response(500);
        }
    }

send_response:
    if (client_wants_keep_alive && conn->current_requests_served < conn->keep_alive_max_requests) {
        set_header(conn->current_response, "Connection", "keep-alive");
        char timeout_buf[32];
        snprintf(timeout_buf, sizeof(timeout_buf), "timeout=%d, max=%d",
                 conn->keep_alive_timeout, conn->keep_alive_max_requests - conn->current_requests_served);
        set_header(conn->current_response, "Keep-Alive", timeout_buf);
    } else {
        set_header(conn->current_response, "Connection", "close");
    }

    char *http_str = serialise_response(conn->current_response);
    if (http_str) {
        send(conn->fd, http_str, strlen(http_str), 0);
        if (conn->current_response->body && conn->current_response->body_length > 0) {
            send(conn->fd, conn->current_response->body, conn->current_response->body_length, 0);
        }
        free(http_str);
    } else {
        const char *internal_error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send(conn->fd, internal_error_response, strlen(internal_error_response), 0);
    }

    conn->current_requests_served++;
    conn->last_activity_time = time(NULL);

    int close_connection = 0;
    const char *response_conn_header = get_response_header(conn->current_response, "Connection");
    if (response_conn_header && strcmp(response_conn_header, "close") == 0) {
        close_connection = 1;
    } else if (conn->current_requests_served >= conn->keep_alive_max_requests) {
        close_connection = 1;
    }

    if (close_connection) {
        remove_connection(conn);
    } else {
        connection_reset(conn);
        conn->state = CONN_STATE_READING_REQUEST;
        struct epoll_event event_mod;
        event_mod.events = EPOLLIN | EPOLLET;
        event_mod.data.ptr = conn;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &event_mod);
    }
}
