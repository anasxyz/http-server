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

#include "../include/config.h"
#include "../include/server.h"
#include "../include/http.h"

#define MAX_EVENTS 1000
#define MAX_BUFFER_SIZE 8192 

void handle_sigint() {
    printf("\nCaught SIGINT, cleaning up...\n");
    free_config();
    exit(0);
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

// Function to free client state
void free_client_state(ClientState *client_state) {
    if (client_state) {
        if (client_state->request) {
            free_request(client_state->request);
        }
        if (client_state->response) {
            free_response(client_state->response);
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
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue; // interrupted by signal
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
                    client_state->fd = client_fd;
                    client_state->read_buffer_len = 0;
                    client_state->request = NULL;
                    client_state->response = NULL;
                    client_state->response_headers_buffer = NULL;
                    client_state->response_headers_sent = 0;
                    client_state->file_send_offset = 0;
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
                            // should probably parse chunk by chunk but for simplicity i just wanna assume
                            // that i get the full header in one go or accumulate.
                            // this part will need more robust parsing logic.
                            if (client_state->read_buffer_len > 4 &&
                                strncmp(client_state->read_buffer + client_state->read_buffer_len - 4, "\r\n\r\n", 4) == 0) {
                                // request headers received
                                client_state->read_buffer[client_state->read_buffer_len] = '\0'; 

                                client_state->state = STATE_PROCESSING_REQUEST;
                                // now transition to handling the request
                                // call a non blocking version of handle_request
                                handle_request(client_state);

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
                                // here i should probably send a 413 Payload Too Large or a 400 Bad Request
                                // but for now i'll just close it, in http.c i'll create a 413 response.
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
                            ssize_t bytes_sent = sendfile(client_fd, client_state->response->file_fd,
                                                          &client_state->file_send_offset,
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
    signal(SIGINT, handle_sigint);

    load_config("server.conf");

    struct Server server =
        server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, PORT, 10, launch);

    server.launch(&server);

    free_config();
    return 0; 
}
