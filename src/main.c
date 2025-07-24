// main.c
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

// Include for ClientState definition
#include "../include/http.h"
#include "../include/config.h"
#include "../include/server.h"

#define MAX_EVENTS 1000
#define MAX_BUFFER_SIZE 8192

// Global flag to indicate a config reload is requested
volatile sig_atomic_t reload_config_flag = 0;

// Existing ClientState definition (assuming it's in http.h or a shared header)
// typedef struct {
//     int fd;
//     char read_buffer[MAX_BUFFER_SIZE];
//     size_t read_buffer_len;
//     HttpRequest *request;
//     HttpResponse *response;
//     char *response_headers_buffer;
//     size_t response_headers_len;
//     size_t response_headers_sent;
//     off_t file_send_offset;
//     enum {
//         STATE_READING_REQUEST,
//         STATE_PROCESSING_REQUEST,
//         STATE_SENDING_HEADERS,
//         STATE_SENDING_BODY,
//         STATE_CLOSING
//     } state;
// } ClientState;


void handle_sigint(int signum) { // Add signum parameter for proper signal handler signature
    printf("\nCaught SIGINT (%d), cleaning up...\n", signum);
    // In a real scenario, you'd signal a graceful shutdown to the event loop
    // For now, exit is fine for SIGINT
    free_global_config(); // Use new function
    exit(0);
}

void handle_sighup(int signum) { // New signal handler for SIGHUP
    printf("\nCaught SIGHUP (%d), setting reload flag...\n", signum);
    reload_config_flag = 1; // Just set the flag, actual reload happens in main loop
}

void make_socket_non_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

void free_client_state(ClientState *client_state) {
    if (client_state) {
        if (client_state->request) {
            free_request(client_state->request);
        }
        if (client_state->response) {
            free_response(client_state->response);
            // After response is freed, close the file_fd if it was open
            if (client_state->response->file_fd != -1) {
                close(client_state->response->file_fd);
            }
        }
        if (client_state->response_headers_buffer) {
            free(client_state->response_headers_buffer);
        }
        free(client_state);
    }
}


void launch(struct Server *server) {
    printf("======== SERVER STARTED ========\n");
    printf("Server listening on http://localhost:%d\n", server->port);
    printf("================================\n");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    make_socket_non_blocking(server->socket);

    struct epoll_event event;
    event.data.fd = server->socket;
    event.events = EPOLLIN | EPOLLET; // listen socket in edge-triggered
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->socket, &event) < 0) {
        perror("epoll_ctl add server socket");
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        // Check for config reload request before epoll_wait
        if (reload_config_flag) {
            printf("Reloading configuration...\n");
            // Attempt to load new config
            Config *new_config = malloc(sizeof(Config));
            if (!new_config) {
                perror("malloc new_config for reload");
                // Log error, continue with old config
            } else {
                if (load_config_into_struct("server.conf", new_config)) {
                    // Successfully loaded new config, now swap
                    Config *old_config = get_current_config(); // Get pointer to active config
                    set_current_config(new_config); // Set new config as active
                    free_config_struct(old_config); // Free the old config's memory
                    printf("Configuration reloaded successfully.\n");
                    // Update server port if it changed (requires re-binding socket, more complex for this project)
                    // For now, assume port doesn't change or require server restart
                    // If port changes, you'd need to close the old listening socket, create a new one,
                    // bind, listen, and add it to epoll. This is very complex for a graceful reload.
                    // For this project, let's assume `PORT` is constant after server start.
                    // If server.port needs to be updated: server->port = get_current_config()->port;
                    // But this only updates the *value*, not the actual bound socket.
                } else {
                    // New config failed to load, log error and keep old config
                    fprintf(stderr, "Failed to load new configuration. Keeping old config.\n");
                    free_config_struct(new_config); // Free the failed new config
                }
            }
            reload_config_flag = 0; // Reset the flag
        }


        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) { // Interrupted by signal (like SIGHUP)
                continue; // Go back to top of loop to check reload_config_flag
            }
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t ev_events = events[i].events;

            if (fd == server->socket) {
                // new connection on the listening socket
                while (1) { // accept all pending connections
                    struct sockaddr_in client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept(server->socket, (struct sockaddr *)&client_addr, &addrlen);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no more incoming connections so just break
                            break;
                        } else {
                            perror("accept");
                            break; // errror accepting
                        }
                    }

                    make_socket_non_blocking(client_fd);

                    ClientState *client_state = malloc(sizeof(ClientState));
                    if (!client_state) {
                        perror("malloc client_state");
                        close(client_fd);
                        continue;
                    }
                    memset(client_state, 0, sizeof(ClientState)); // Initialize all members to 0/NULL
                    client_state->fd = client_fd;
                    client_state->state = STATE_READING_REQUEST;

                    struct epoll_event client_event;
                    client_event.data.ptr = client_state; // store client state
                    client_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // edge triggered and also detect peer close
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0) {
                        perror("epoll_ctl add client_fd");
                        free_client_state(client_state);
                        close(client_fd);
                        continue;
                    }
                }
            } else {
                // existing client event
                ClientState *client_state = (ClientState *)events[i].data.ptr;
                int client_fd = client_state->fd;

                // handle error conditions like EPOLLERR, EPOLLHUP, EPOLLRDHUP
                if ((ev_events & EPOLLERR) || (ev_events & EPOLLHUP) || (ev_events & EPOLLRDHUP)) {
                    fprintf(stderr, "Client %d disconnected or error.\n", client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    free_client_state(client_state);
                    continue;
                }

                if (ev_events & EPOLLIN) {
                    // data available for reading
                    while (1) {
                        // Use client_state->read_buffer_len to append data, not overwrite
                        ssize_t bytes_read = recv(client_fd, client_state->read_buffer + client_state->read_buffer_len,
                                                  MAX_BUFFER_SIZE - 1 - client_state->read_buffer_len, 0);
                        if (bytes_read == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // no more data to read for now
                                break;
                            } else {
                                perror("recv");
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                free_client_state(client_state);
                                goto next_event; // move to the next epoll event
                            }
                        } else if (bytes_read == 0) {
                            // client closed connection
                            fprintf(stderr, "Client %d closed connection.\n", client_fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                            close(client_fd);
                            free_client_state(client_state);
                            goto next_event;
                        } else {
                            client_state->read_buffer_len += bytes_read;
                            // check for full request like double CRLF for HTTP headers and stuff like that
                            if (client_state->read_buffer_len >= 4 &&
                                strncmp(client_state->read_buffer + client_state->read_buffer_len - 4, "\r\n\r\n", 4) == 0) {
                                // request headers received
                                client_state->read_buffer[client_state->read_buffer_len] = '\0';

                                client_state->state = STATE_PROCESSING_REQUEST;
                                // now transition to handling the request
                                handle_request(client_state); // Assuming handle_request still takes ClientState*

                                // if response is ready change epoll_ctl to monitor EPOLLOUT
                                if (client_state->response_headers_buffer) {
                                    struct epoll_event mod_event;
                                    mod_event.data.ptr = client_state;
                                    mod_event.events = EPOLLOUT | EPOLLET | EPOLLRDHUP; // now i can start writing
                                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &mod_event) == -1) {
                                        perror("epoll_ctl MOD for write");
                                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                        close(client_fd);
                                        free_client_state(client_state);
                                    }
                                } else {
                                    // if no response ready then just close
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                    close(client_fd);
                                    free_client_state(client_state);
                                }
                                break; // break out of inner while loop for reading
                            }
                            // if buffer is full but no \r\n\r\n then handle error or larger buffer
                            if (client_state->read_buffer_len >= MAX_BUFFER_SIZE - 1) {
                                fprintf(stderr, "Client %d request too large or malformed.\n", client_fd);
                                // For simplicity, just close here. http.c could generate 413.
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                free_client_state(client_state);
                                break;
                            }
                        }
                    }
                }

                if (ev_events & EPOLLOUT) {
                    // socket is ready for writing
                    if (client_state->state == STATE_SENDING_HEADERS) {
                        ssize_t bytes_sent = send(client_fd, client_state->response_headers_buffer + client_state->response_headers_sent,
                                                  client_state->response_headers_len - client_state->response_headers_sent, 0);

                        if (bytes_sent == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // socket buffer full so should probably try try again later
                                continue;
                            } else {
                                perror("send headers");
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                free_client_state(client_state);
                                goto next_event;
                            }
                        }
                        client_state->response_headers_sent += bytes_sent;

                        if (client_state->response_headers_sent == client_state->response_headers_len) {
                            // headers fully sent
                            free(client_state->response_headers_buffer); // now frree the header buffer
                            client_state->response_headers_buffer = NULL; // and clear pointer

                            if (client_state->response && client_state->response->file_fd != -1) {
                                client_state->state = STATE_SENDING_BODY;
                                // remain in EPOLLOUT to send body
                            } else {
                                // no body to send so justclose connection
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                free_client_state(client_state);
                                goto next_event;
                            }
                        }
                    }

                    if (client_state->state == STATE_SENDING_BODY) {
                        if (client_state->response && client_state->response->file_fd != -1) {
                            // The offset argument to sendfile is a pointer to a loff_t.
                            // Ensure client_state->file_send_offset is loff_t or cast.
                            ssize_t bytes_sent = sendfile(client_fd, client_state->response->file_fd,
                                                          &client_state->file_send_offset, // Correct usage
                                                          client_state->response->file_size - client_state->file_send_offset);

                            if (bytes_sent == -1) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    // socket buffer full so try again later
                                    continue;
                                } else {
                                    perror("sendfile");
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                    close(client_fd);
                                    free_client_state(client_state);
                                    goto next_event;
                                }
                            }

                            if (client_state->file_send_offset >= client_state->response->file_size) {
                                // file fully sent
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                                close(client_fd);
                                free_client_state(client_state);
                                goto next_event;
                            }
                        } else {
                            // no file to send
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                            close(client_fd);
                            free_client_state(client_state);
                            goto next_event;
                        }
                    }
                }
            }
            next_event:; // just a label for the goto statement to continue to the next epoll event
        }
    }
}

int main() {
    // Register signal handlers
    struct sigaction sa_int = {0};
    sa_int.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_hup = {0};
    sa_hup.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa_hup, NULL);


    // Initial config load
    // The load_config function will now populate a global Config struct
    Config *initial_config = malloc(sizeof(Config));
    if (!initial_config) {
        perror("malloc initial_config");
        exit(EXIT_FAILURE);
    }
    if (!load_config_into_struct("server.conf", initial_config)) {
        fprintf(stderr, "Failed to load initial configuration. Exiting.\n");
        free(initial_config);
        exit(EXIT_FAILURE);
    }
    set_current_config(initial_config); // Set this as the active config

    // Get current port from the active configuration
    // This assumes server_constructor will use the global PORT macro,
    // which needs to be updated to use the dynamic config
    // For now, if PORT is a global variable directly from config.c, it won't reflect dynamic changes here.
    // It's better if server_constructor takes a Config* or at least the port from the config.
    // As a temporary workaround, if PORT is still a global, you'd need to assume it's set by load_config_into_struct
    // before server_constructor is called.
    struct Server server =
        server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, get_current_config()->port, 10, launch);

    server.launch(&server);

    free_global_config(); // Use new function for cleanup on exit
    return 0;
}
