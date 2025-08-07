#include <errno.h> // For errno and EWOULDBLOCK/EAGAIN
#include <fcntl.h> // For fcntl (file control) to set non-blocking mode
#include <glib.h>
#include <netinet/in.h> // For sockaddr_in structure
#include <signal.h>     // For signal handling (SIGINT)
#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit, malloc, free
#include <string.h>     // For string manipulation like memset, strlen, strstr
#include <sys/epoll.h>  // For epoll functions
#include <sys/select.h> // For FD_SETSIZE, a common max FD limit
#include <sys/socket.h> // For socket, bind, listen, accept functions
#include <time.h>       // For time() function to get current time
#include <unistd.h>     // For close function, read, write

#include "minheap_util.h"
#include "parser.h"
#include "server.h"

// Global counter for active client connections
static int active_clients_count = 0;

// Global flag to control the main server loop
volatile int running = 1;

GHashTable *client_states_map = NULL;

// --- The NEW state transition function ---
void transition_state(int epoll_fd, client_state_t *client, client_state_enum_t new_state) {
    if (client->state == new_state) {
        return; // No change needed
    }

    printf("Client %d transitioning from state %d to state %d.\n", client->fd, client->state, new_state);
    client->state = new_state;

    struct epoll_event new_event;
    new_event.data.ptr = client;

    switch (new_state) {
        case STATE_READING_REQUEST:
            // Prepare for a new read on a keep-alive connection
            client->in_buffer_len = 0;
            client->out_buffer_len = 0;
            client->out_buffer_sent = 0;

            // Add to heap for timeout
            time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
            add_timeout(client->fd, expires_at);

            // Modify epoll interest back to EPOLLIN
            new_event.events = EPOLLIN | EPOLLET;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event);
            break;

        case STATE_READING_BODY:
            // No epoll_ctl change needed here, we're still reading from EPOLLIN
            printf("Client %d is now in the READING_BODY state.\n", client->fd);
            break;
            
        case STATE_WRITING_RESPONSE:
            // Modify epoll interest to EPOLLOUT to begin writing
            new_event.events = EPOLLOUT | EPOLLET;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &new_event);
            break;

        case STATE_IDLE:
            // The idle state is a special case. We transition to it after a write,
            // but the `STATE_READING_REQUEST` transition is where the heap is managed.
            printf("Client %d is now IDLE, awaiting next request or timeout.\n", client->fd);
            break;
            
        case STATE_CLOSED:
            // Centralized cleanup
            close_client_connection(epoll_fd, client);
            break;
    }
}

// Function to set a file descriptor (like a socket) to non-blocking mode.
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK failed");
        return -1;
    }
    return 0;
}

// Function to set up the listening socket
int setup_listening_socket(int port) {
    int listen_sock;
    struct sockaddr_in server_addr;
    int opt = 1;

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket creation failed");
        return -1;
    }

    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR failed");
        close(listen_sock);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(listen_sock);
        return -1;
    }

    if (listen(listen_sock, 10) == -1) {
        perror("listen failed");
        close(listen_sock);
        return -1;
    }

    printf("Server is listening for connections on port %d...\n", port);

    if (set_nonblocking(listen_sock) == -1) {
        close(listen_sock);
        return -1;
    }

    return listen_sock;
}

// Function to handle a new incoming connection on the listening socket.
void handle_new_connection(int listen_sock, int epoll_fd) {
    int conn_sock;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    struct epoll_event event;
    client_state_t *client_state;

    while (1) {
        client_len = sizeof(client_addr);
        conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (conn_sock == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                perror("accept failed");
                exit(EXIT_FAILURE);
            }
        }

        printf("New connection accepted on socket %d. Active clients: %d\n", conn_sock, active_clients_count + 1);

        if (set_nonblocking(conn_sock) == -1) {
            close(conn_sock);
            continue;
        }

        client_state = (client_state_t *)malloc(sizeof(client_state_t));
        if (client_state == NULL) {
            perror("malloc client_state failed");
            close(conn_sock);
            continue;
        }
        memset(client_state, 0, sizeof(client_state_t));
        client_state->fd = conn_sock;
        client_state->state = STATE_READING_REQUEST; // Initial state
        client_state->last_activity_time = time(NULL);
        client_state->timeout_heap_index = -1; 

        g_hash_table_insert(client_states_map, GINT_TO_POINTER(conn_sock), client_state);

        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = client_state; 
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
            perror("epoll_ctl: adding conn_sock failed");
            g_hash_table_remove(client_states_map, GINT_TO_POINTER(conn_sock));
            free(client_state);
            close(conn_sock);
            continue;
        }

        active_clients_count++;
    }
}

// Function to handle reading data from a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_read_event(client_state_t *client_state, int epoll_fd) {
    int current_fd = client_state->fd;

    // This check is now handled by the transition_state logic
    if (client_state->state == STATE_IDLE) {
        remove_timeout_by_fd(current_fd);
    }
    
    while (1) {
        ssize_t bytes_transferred;
        size_t bytes_to_read;
        char *read_destination;

        // Determine where to read based on the current state
        if (client_state->state == STATE_READING_REQUEST) {
            bytes_to_read = sizeof(client_state->in_buffer) - 1 - client_state->in_buffer_len;
            read_destination = client_state->in_buffer + client_state->in_buffer_len;
        } else if (client_state->state == STATE_READING_BODY) {
            bytes_to_read = client_state->content_length - client_state->body_received;
            read_destination = client_state->body_buffer + client_state->body_received;
        } else {
            break; // Not in a reading state
        }

        if (bytes_to_read == 0) {
            break;
        }

        bytes_transferred = read(current_fd, read_destination, bytes_to_read);

        if (bytes_transferred == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                perror("read client socket failed");
                return 1;
            }
        } else if (bytes_transferred == 0) {
            printf("Client socket %d closed connection during read.\n", current_fd);
            return 1;
        }

        client_state->last_activity_time = time(NULL);

        if (client_state->state == STATE_READING_REQUEST) {
            client_state->in_buffer_len += bytes_transferred;
            client_state->in_buffer[client_state->in_buffer_len] = '\0';

            if (strstr(client_state->in_buffer, "\r\n\r\n")) {
                parse_http_request(client_state);
                if (client_state->content_length > 0) {
                    transition_state(epoll_fd, client_state, STATE_READING_BODY);
                } else {
                    create_http_response(client_state);
                    transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
                }
            }
        } else if (client_state->state == STATE_READING_BODY) {
            client_state->body_received += bytes_transferred;
            if (client_state->body_received >= client_state->content_length) {
                client_state->body_buffer[client_state->content_length] = '\0';
                printf("Full body received for client %d. Total size: %zu.\n", current_fd, client_state->content_length);
                printf("Final Request Body:\n%s\n", client_state->body_buffer);
                create_http_response(client_state);
                transition_state(epoll_fd, client_state, STATE_WRITING_RESPONSE);
            }
        }
    }
    return 0;
}

// Function to handle writing data to a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_write_event(client_state_t *client_state, int epoll_fd) {
    int current_fd = client_state->fd;
    ssize_t bytes_transferred;

    if (client_state->state != STATE_WRITING_RESPONSE) {
        printf("Client %d received EPOLLOUT but not in WRITING_RESPONSE state. Closing.\n", current_fd);
        transition_state(epoll_fd, client_state, STATE_CLOSED);
        return 0;
    }

    size_t remaining_to_send = client_state->out_buffer_len - client_state->out_buffer_sent;

    if (remaining_to_send > 0) {
        bytes_transferred = write(current_fd, client_state->out_buffer + client_state->out_buffer_sent, remaining_to_send);

        if (bytes_transferred == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Client socket %d send buffer full, retrying write.\n", current_fd);
                return 0;
            } else {
                perror("write client socket failed");
                transition_state(epoll_fd, client_state, STATE_CLOSED);
                return 0;
            }
        } else if (bytes_transferred == 0) {
            printf("Client socket %d closed connection during write.\n", current_fd);
            transition_state(epoll_fd, client_state, STATE_CLOSED);
            return 0;
        } else {
            client_state->out_buffer_sent += bytes_transferred;
            printf("Sent %zd bytes to client %d. Total sent: %zu/%zu.\n", bytes_transferred, current_fd, client_state->out_buffer_sent, client_state->out_buffer_len);

            client_state->last_activity_time = time(NULL);

            if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
                printf("All response data sent to client %d.\n", current_fd);
                if (client_state->keep_alive) {
                    transition_state(epoll_fd, client_state, STATE_READING_REQUEST);
                } else {
                    printf("Closing connection %d (Keep-Alive not requested).\n", current_fd);
                    transition_state(epoll_fd, client_state, STATE_CLOSED);
                }
            }
            return 0;
        }
    } else {
        printf("EPOLLOUT on client %d but nothing to send. Closing.\n", current_fd);
        transition_state(epoll_fd, client_state, STATE_CLOSED);
        return 0;
    }
}

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state) {
    if (client_state == NULL) {
        return;
    }

    int current_fd = client_state->fd;

    // Remove the client from the timeout heap
    if (client_state->timeout_heap_index != -1) {
        remove_timeout_by_fd(current_fd);
    }
    
    if (client_state->body_buffer != NULL) {
        free(client_state->body_buffer);
        client_state->body_buffer = NULL;
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 && errno != ENOENT) {
        perror("epoll_ctl: DEL client socket failed");
    }

    close(current_fd);

    g_hash_table_remove(client_states_map, GINT_TO_POINTER(current_fd));

    free(client_state);
    printf("Client socket %d fully closed and removed.\n", current_fd);

    active_clients_count--;
    printf("Active clients: %d\n", active_clients_count);
}

// Function to handle events on an existing client socket.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr) {
    client_state_t *client_state = (client_state_t *)event_ptr->data.ptr;
    if (client_state == NULL) {
        fprintf(stderr, "handle_client_event: client_state is NULL.\n");
        return;
    }
    int current_fd = client_state->fd;
    uint32_t event_flags = event_ptr->events;

    if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        printf("Client socket %d disconnected or error occurred.\n", current_fd);
        transition_state(epoll_fd, client_state, STATE_CLOSED);
    } else if (event_flags & EPOLLIN) {
        // Now handle_read_event returns 0 to not close the connection directly
        handle_read_event(client_state, epoll_fd);
    } else if (event_flags & EPOLLOUT) {
        // Now handle_write_event returns 0 to not close the connection directly
        handle_write_event(client_state, epoll_fd);
    }
}

// Signal handler function for SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    printf("\nSIGINT received. Shutting down server gracefully...\n");
    running = 0;
}

void print_heap_state() {
    printf("Timeout Heap State (Size: %zu):\n", heap_size);
    if (heap_size == 0) {
        printf("  [Empty]\n");
        return;
    }
    for (size_t i = 0; i < heap_size; ++i) {
        printf("  [%zu]: FD %d, Expires: %ld\n", i, timeout_heap[i].fd, timeout_heap[i].expires);
    }
}

int main() {
    int listen_sock;
    int epoll_fd;
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];
    int num_events;
    int i;

    client_states_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    printf("Starting simple epoll server on port %d.\n", PORT);

    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal registration failed");
        exit(EXIT_FAILURE);
    }

    listen_sock = setup_listening_socket(PORT);
    if (listen_sock == -1) {
        exit(EXIT_FAILURE);
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listen_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        perror("epoll_ctl: adding listen_sock failed");
        close(listen_sock);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    while (running) {
        long epoll_timeout_ms = get_next_timeout_ms();

        printf("--- Main Loop Pass ---\n");
        print_heap_state();
        printf("Next epoll_wait timeout: %ldms\n", epoll_timeout_ms);
        
        num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_timeout_ms);

        if (num_events == -1) {
            if (errno == EINTR) {
                if (!running) {
                    break;
                }
                continue;
            }
            perror("epoll_wait failed");
            running = 0;
            continue;
        }

        for (i = 0; i < num_events; i++) {
            if (events[i].data.fd == listen_sock) {
                handle_new_connection(listen_sock, epoll_fd);
            } else {
                handle_client_event(epoll_fd, &events[i]);
            }
        }

        // Process any expired timeouts after epoll_wait returns
        time_t current_time;
        while (heap_size > 0) {
            current_time = time(NULL);

            if (timeout_heap[0].expires > current_time) {
                printf("DEBUG: Next timeout for FD %d is in the future. Remaining: %ld seconds.\n", 
                       timeout_heap[0].fd, timeout_heap[0].expires - current_time);
                break;
            }

            int expired_fd = timeout_heap[0].fd;
            printf("DEBUG: Found expired timeout for FD %d. Current time: %ld, Expires: %ld.\n", 
                   expired_fd, current_time, timeout_heap[0].expires);

            client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(expired_fd));

            if (client_state != NULL) {
                printf("DEBUG: Found client state for FD %d. Closing connection.\n", expired_fd);
                close_client_connection(epoll_fd, client_state);
            } else {
                printf("WARNING: Expired client FD %d not found in hash table. This is a logic error.\n", expired_fd);
            }

            remove_min_timeout();
            printf("DEBUG: Removed expired FD %d from min-heap. New heap size: %zu.\n", expired_fd, heap_size);
        }
    }

    printf("Server shutting down. Cleaning up resources...\n");
    close(listen_sock);

    g_hash_table_destroy(client_states_map);

    close(epoll_fd);
    printf("Server shutdown complete.\n");

    return 0;
}
