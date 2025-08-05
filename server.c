#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit, malloc, free
#include <string.h>     // For string manipulation like memset, strlen, strstr
#include <unistd.h>     // For close function, read, write
#include <sys/socket.h> // For socket, bind, listen, accept functions
#include <netinet/in.h> // For sockaddr_in structure
#include <fcntl.h>      // For fcntl (file control) to set non-blocking mode
#include <sys/epoll.h>  // For epoll functions
#include <errno.h>      // For errno and EWOULDBLOCK/EAGAIN
#include <time.h>       // For time() function to get current time
#include <sys/select.h> // For FD_SETSIZE, a common max FD limit
#include <signal.h>     // --- NEW: For signal handling (SIGINT) ---

// Define the port number for our server
#define PORT 8080
// Define the maximum number of events epoll_wait can return at once
#define MAX_EVENTS 10
// Define buffer sizes for client input/output
#define MAX_BUFFER_SIZE 4096

// Define a hard cap for active clients to prevent memory exhaustion
#define MAX_ACTIVE_CLIENTS 100 // Example: Allow up to 100 concurrent clients

// Define the Keep-Alive idle timeout in seconds
#define KEEPALIVE_IDLE_TIMEOUT_SECONDS 5 // Close connection if idle for 5 seconds

// Global counter for active client connections
static int active_clients_count = 0;

// --- NEW: Global flag to control the main server loop ---
// 'volatile' ensures the compiler doesn't optimize away checks on this variable,
// as it can be changed by an external signal handler.
volatile int running = 1;

// --- Define states for our simple state machine ---
typedef enum {
    READING_REQUEST,
    WRITING_RESPONSE,
    // CLOSING_CONNECTION is handled implicitly by returning 1 from handler functions
    // indicating the connection should be closed.
} client_state_enum;

// --- Structure to hold per-client state ---
typedef struct {
    int fd;                     // The client's file descriptor
    client_state_enum state;    // Current state of this client's interaction
    char in_buffer[MAX_BUFFER_SIZE]; // Buffer for incoming data
    size_t in_buffer_len;       // Current length of data in in_buffer
    char out_buffer[MAX_BUFFER_SIZE]; // Buffer for outgoing data
    size_t out_buffer_len;      // Total length of data to send from out_buffer
    size_t out_buffer_sent;     // How much of out_buffer has already been sent
    int keep_alive;             // Flag to indicate if keep-alive is enabled for this connection (0 or 1)
    time_t last_activity_time;  // Timestamp of the last activity on this connection
} client_state_t;

// Global array to map file descriptors to client state structs
// FD_SETSIZE is a common system-defined limit for file descriptors (often 1024).
// This allows us to quickly find a client's state by its FD.
static client_state_t *client_states_map[FD_SETSIZE];

// Function to set a file descriptor (like a socket) to non-blocking mode.
// This means operations like 'accept' or 'read' on this descriptor won't
// pause the program if they can't complete immediately.
int set_nonblocking(int fd) {
    // Get the current flags associated with the file descriptor.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return -1;
    }
    // Add the O_NONBLOCK flag to the existing flags.
    // O_NONBLOCK tells the OS that I/O operations should return immediately
    // if they would block, typically with an EAGAIN or EWOULDBLOCK error.
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK failed");
        return -1;
    }
    return 0; // Return 0 on success, -1 on failure
}

// Function to set up the listening socket (create, bind, listen, set non-blocking)
// Returns the listening socket FD on success, -1 on failure.
int setup_listening_socket(int port) {
    int listen_sock;
    struct sockaddr_in server_addr;
    int opt = 1;

    // 1. Create a listening socket
    // AF_INET: Use IPv4 addresses
    // SOCK_STREAM: Use TCP (reliable, connection-oriented)
    // 0: Let the system choose the appropriate protocol (TCP for SOCK_STREAM)
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket creation failed");
        return -1;
    }

    // Optional: Set SO_REUSEADDR to allow immediate reuse of the port
    // This is useful during development to avoid "Address already in use" errors
    // if the server is restarted quickly.
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR failed");
        close(listen_sock);
        return -1;
    }

    // Prepare the server address structure
    memset(&server_addr, 0, sizeof(server_addr)); // Clear the structure to zeros
    server_addr.sin_family = AF_INET;             // IPv4 address family
    server_addr.sin_addr.s_addr = INADDR_ANY;     // Listen on all available network interfaces
    server_addr.sin_port = htons(port);           // Convert port number to network byte order

    // 2. Bind the listening socket to the specified IP address and port
    // This assigns the address to the socket.
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(listen_sock);
        return -1;
    }

    // 3. Start listening for incoming connections
    // 10: The maximum length of the queue of pending connections.
    if (listen(listen_sock, 10) == -1) {
        perror("listen failed");
        close(listen_sock);
        return -1;
    }

    printf("Server is listening for connections on port %d...\n", port);

    // 4. Set the listening socket to non-blocking mode
    // This is crucial for epoll. 'accept' on this socket won't block the server.
    if (set_nonblocking(listen_sock) == -1) {
        close(listen_sock);
        return -1;
    }

    return listen_sock;
}

// Function to handle a new incoming connection on the listening socket.
// It accepts all pending connections and adds them to the epoll instance.
void handle_new_connection(int listen_sock, int epoll_fd) {
    int conn_sock;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    struct epoll_event event;
    client_state_t *client_state; // Pointer to our client state struct

    // Loop to accept all pending connections.
    // In edge-triggered mode (EPOLLET), it's crucial to accept *all*
    // connections that have arrived, otherwise epoll won't notify
    // us again for these pending connections.
    while (1) {
        client_len = sizeof(client_addr);
        conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (conn_sock == -1) {
            // If accept returns EAGAIN or EWOULDBLOCK, it means there are
            // no more pending connections to accept right now.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Exit the accept loop
            } else {
                perror("accept failed");
                // In a real server, you might log this error and continue,
                // rather than exiting, to keep the server running.
                exit(EXIT_FAILURE); // For simplicity, we exit on other errors
            }
        }

        // Check if we have reached the maximum client limit
        if (active_clients_count >= MAX_ACTIVE_CLIENTS) {
            printf("Client limit reached (%d). Rejecting new connection on socket %d.\n", MAX_ACTIVE_CLIENTS, conn_sock);
            close(conn_sock); // Close the new connection immediately
            continue;         // Try to accept next if any, but don't process this one
        }

        // A new connection has been successfully accepted!
        printf("New connection accepted on socket %d. Active clients: %d/%d\n", conn_sock, active_clients_count + 1, MAX_ACTIVE_CLIENTS);

        // Set the newly accepted client socket to non-blocking mode.
        // This ensures that future read/write operations on this client
        // socket won't block the entire server.
        if (set_nonblocking(conn_sock) == -1) {
            close(conn_sock); // Close the problematic socket
            continue;         // Skip to the next potential connection
        }

        // Allocate and initialize client state
        client_state = (client_state_t *)malloc(sizeof(client_state_t));
        if (client_state == NULL) {
            perror("malloc client_state failed");
            close(conn_sock);
            continue;
        }
        memset(client_state, 0, sizeof(client_state_t)); // Clear all fields
        client_state->fd = conn_sock;
        client_state->state = READING_REQUEST; // Initial state for new connection
        client_state->in_buffer_len = 0;
        client_state->out_buffer_len = 0;
        client_state->out_buffer_sent = 0;
        client_state->keep_alive = 0; // Initialize keep-alive flag to false
        client_state->last_activity_time = time(NULL); // Set initial activity time


        // Add the new client socket to the epoll instance.
        // We want to be notified when this client socket has data to read (EPOLLIN).
        // We will add EPOLLOUT later when we have a response to send.
        // epoll_event.data.ptr now points to our client_state_t
        event.events = EPOLLIN | EPOLLET; // Watch for input, edge-triggered
        event.data.ptr = client_state;    // Associate the event with our client_state_t
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
            perror("epoll_ctl: adding conn_sock failed");
            free(client_state); // Free memory if adding fails
            close(conn_sock);   // Close the problematic socket
            continue;           // Skip to the next potential connection
        }

        // Increment active client count upon successful addition
        active_clients_count++;
        // Store client state in the global map
        if (conn_sock < FD_SETSIZE) { // Ensure FD is within our map bounds
            client_states_map[conn_sock] = client_state;
        } else {
            fprintf(stderr, "Warning: Client FD %d exceeds FD_SETSIZE. Cannot track for timeout.\n", conn_sock);
            // In a real server, you'd handle this more robustly (e.g., close connection, use a different map)
        }
    }
}

// Function to handle reading data from a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_read_event(client_state_t *client_state, int epoll_fd) {
    int current_fd = client_state->fd;
    ssize_t bytes_transferred;

    // Only read if we are in the READING_REQUEST state
    if (client_state->state != READING_REQUEST) {
        // This shouldn't happen if state machine is correct, but defensive check
        printf("Client %d received EPOLLIN but not in READING_REQUEST state. Closing.\n", current_fd);
        return 1; // Close connection as something unexpected happened
    }

    // In edge-triggered mode, it's crucial to read ALL available data
    // until read() returns EAGAIN/EWOULDBLOCK or 0 (EOF).
    while (1) {
        // Read into the buffer, ensuring we don't overflow
        bytes_transferred = read(current_fd,
                                 client_state->in_buffer + client_state->in_buffer_len,
                                 sizeof(client_state->in_buffer) - 1 - client_state->in_buffer_len);

        if (bytes_transferred == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data to read for now. Connection is still open.
                break; // Exit read loop, wait for next event
            } else {
                perror("read client socket failed");
                return 1; // Fatal read error, mark for closure
            }
        } else if (bytes_transferred == 0) {
            // Client closed their side of the connection (End-of-File)
            printf("Client socket %d closed connection during read.\n", current_fd);
            return 1; // Mark for closure
        } else {
            // Successfully read some data
            client_state->in_buffer_len += bytes_transferred;
            client_state->in_buffer[client_state->in_buffer_len] = '\0'; // Null-terminate for printing

            printf("Received %zd bytes from client %d. Total: %zu. Data: '%s'\n",
                   bytes_transferred, current_fd, client_state->in_buffer_len, client_state->in_buffer);

            client_state->last_activity_time = time(NULL); // Update activity time on read

            // Simple HTTP request completion check
            // For simplicity, we check for "\r\n\r\n" to signify end of headers.
            // A real parser would be more robust.
            if (strstr(client_state->in_buffer, "\r\n\r\n") != NULL) {
                printf("Full HTTP request received from client %d. Preparing response.\n", current_fd);

                // Check for Keep-Alive header
                if (strstr(client_state->in_buffer, "Connection: keep-alive") != NULL) {
                    client_state->keep_alive = 1;
                    printf("Client %d requested Keep-Alive.\n", current_fd);
                } else {
                    client_state->keep_alive = 0;
                    printf("Client %d did not request Keep-Alive.\n", current_fd);
                }

                // Prepare the HTTP response
                const char *http_response_body = "Hello World!";
                char http_headers[256]; // Buffer for headers
                
                // Construct basic headers
                snprintf(http_headers, sizeof(http_headers),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html\r\n"
                         "Content-Length: %zu\r\n",
                         strlen(http_response_body));

                // Append Keep-Alive header if requested
                if (client_state->keep_alive) {
                    strcat(http_headers, "Connection: keep-alive\r\n");
                    // Optionally add Keep-Alive timeout/max headers for client guidance
                    char keep_alive_info[64];
                    snprintf(keep_alive_info, sizeof(keep_alive_info), "Keep-Alive: timeout=%d, max=100\r\n", KEEPALIVE_IDLE_TIMEOUT_SECONDS);
                    strcat(http_headers, keep_alive_info);
                }
                
                // Finalize headers and append body
                snprintf(client_state->out_buffer, sizeof(client_state->out_buffer),
                         "%s\r\n%s",
                         http_headers, http_response_body);

                client_state->out_buffer_len = strlen(client_state->out_buffer);
                client_state->out_buffer_sent = 0; // Reset sent counter

                client_state->state = WRITING_RESPONSE; // Change state

                // Modify epoll interest to EPOLLOUT
                // We are done reading, now we want to write.
                struct epoll_event new_event;
                new_event.events = EPOLLOUT | EPOLLET; // Now primarily interested in writing
                new_event.data.ptr = client_state;     // Keep the same client_state pointer
                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, current_fd, &new_event) == -1) {
                    perror("epoll_ctl: MOD to EPOLLOUT failed");
                    return 1; // Mark for closure on error
                }
                break; // Exit read loop, as we've processed the request and changed state
            }

            // If buffer is full and no full request, it's an error or too large request
            if (client_state->in_buffer_len >= (sizeof(client_state->in_buffer) - 1)) {
                printf("Client %d input buffer full, no complete request. Closing.\n", current_fd);
                return 1;
            }
        }
    }
    return 0; // Connection remains open
}

// Function to handle writing data to a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_write_event(client_state_t *client_state, int epoll_fd) {
    int current_fd = client_state->fd;
    ssize_t bytes_transferred;

    // Only write if we are in the WRITING_RESPONSE state
    if (client_state->state != WRITING_RESPONSE) {
        // This shouldn't happen if state machine is correct, but defensive check
        printf("Client %d received EPOLLOUT but not in WRITING_RESPONSE state. Closing.\n", current_fd);
        return 1; // Close connection
    }

    // Calculate how much data is remaining to send
    size_t remaining_to_send = client_state->out_buffer_len - client_state->out_buffer_sent;

    if (remaining_to_send > 0) {
        // Attempt to send the remaining data
        bytes_transferred = write(current_fd,
                                  client_state->out_buffer + client_state->out_buffer_sent,
                                  remaining_to_send);

        if (bytes_transferred == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel send buffer is full, try again later when EPOLLOUT fires again.
                printf("Client socket %d send buffer full, retrying write.\n", current_fd);
                return 0; // Connection remains open, wait for next EPOLLOUT
            } else {
                perror("write client socket failed");
                return 1; // Fatal write error, mark for closure
            }
        } else if (bytes_transferred == 0) {
            // This typically means the connection was closed by peer during write.
            printf("Client socket %d closed connection during write.\n", current_fd);
            return 1;
        } else {
            // Successfully sent some data
            client_state->out_buffer_sent += bytes_transferred;
            printf("Sent %zd bytes to client %d. Total sent: %zu/%zu.\n",
                   bytes_transferred, current_fd, client_state->out_buffer_sent, client_state->out_buffer_len);

            client_state->last_activity_time = time(NULL); // Update activity time on write

            // Check if all data has been sent
            if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
                printf("All response data sent to client %d.\n", current_fd);
                // Decision based on Keep-Alive
                if (client_state->keep_alive) {
                    printf("Keeping connection %d alive for next request.\n", current_fd);
                    client_state->state = READING_REQUEST; // Reset state for next request
                    client_state->in_buffer_len = 0;       // Clear input buffer
                    client_state->out_buffer_len = 0;      // Clear output buffer state
                    client_state->out_buffer_sent = 0;

                    // Modify epoll interest back to EPOLLIN
                    struct epoll_event new_event;
                    new_event.events = EPOLLIN | EPOLLET; // Now interested in reading again
                    new_event.data.ptr = client_state;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, current_fd, &new_event) == -1) {
                        perror("epoll_ctl: MOD to EPOLLIN for keep-alive failed");
                        return 1; // Close on error
                    }
                    return 0; // Connection kept alive
                } else {
                    printf("Closing connection %d (Keep-Alive not requested).\n", current_fd);
                    return 1; // All done, mark for closure
                }
            }
            // If not all sent, EPOLLOUT will fire again (due to EPOLLET) when more buffer space is available.
            return 0; // Connection remains open, more data to send
        }
    } else {
        // This case should ideally not be reached if logic is correct,
        // but means EPOLLOUT fired when there was nothing to send.
        printf("EPOLLOUT on client %d but nothing to send. Closing.\n", current_fd);
        return 1;
    }
}

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state) {
    int current_fd = client_state->fd;

    // Attempt to remove the file descriptor from the epoll instance FIRST.
    // Check for ENOENT (No such file or directory) which means the FD was
    // already implicitly removed or never added, which is not a critical error here.
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 && errno != ENOENT) {
         perror("epoll_ctl: DEL client socket failed"); // Only print if it's a real error
    }
    close(current_fd); // Now close the socket itself.
    
    // Remove client state from the global map before freeing
    if (current_fd < FD_SETSIZE) {
        client_states_map[current_fd] = NULL;
    }

    free(client_state); // Free the allocated client state memory
    printf("Client socket %d fully closed and removed from epoll.\n", current_fd);

    // Decrement active client count upon closure
    active_clients_count--;
    printf("Active clients: %d/%d\n", active_clients_count, MAX_ACTIVE_CLIENTS);
}


// Function to handle events on an existing client socket.
// It orchestrates calls to read, write, or close functions based on event flags and client state.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr) {
    client_state_t *client_state = (client_state_t *)event_ptr->data.ptr;
    int current_fd = client_state->fd;
    uint32_t event_flags = event_ptr->events;
    int should_close = 0; // Flag to indicate if the connection should be closed

    // Check for connection closed by peer (EPOLLRDHUP), hang up (EPOLLHUP), or error (EPOLLERR)
    if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        printf("Client socket %d disconnected or error occurred.\n", current_fd);
        should_close = 1; // Mark for closure
    }
    // Handle EPOLLIN event: Data available to read
    else if (event_flags & EPOLLIN) {
        should_close = handle_read_event(client_state, epoll_fd);
    }
    // Handle EPOLLOUT event: Socket is ready for writing.
    else if (event_flags & EPOLLOUT) {
        should_close = handle_write_event(client_state, epoll_fd);
    }

    // If any of the handlers or initial checks determined the connection should be closed
    if (should_close) {
        close_client_connection(epoll_fd, client_state);
    }
}

// --- NEW: Signal handler function for SIGINT (Ctrl+C) ---
void handle_sigint(int sig) {
    printf("\nSIGINT received. Shutting down server gracefully...\n");
    running = 0; // Set the global flag to stop the main loop
}


int main() {
    int listen_sock;            // The socket that listens for new incoming connections
    int epoll_fd;               // The file descriptor for our epoll instance
    struct epoll_event event;   // A single epoll_event structure to configure events
    struct epoll_event events[MAX_EVENTS]; // Array to store events returned by epoll_wait
    int num_events;             // Number of events returned by epoll_wait
    int i;                      // Loop counter for processing events array

    printf("Starting simple epoll server on port %d.\n", PORT);

    // --- NEW: Register the signal handler for SIGINT ---
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal registration failed");
        exit(EXIT_FAILURE);
    }

    // Initialize the client_states_map to NULL
    for (i = 0; i < FD_SETSIZE; i++) {
        client_states_map[i] = NULL;
    }

    // Setup the listening socket
    listen_sock = setup_listening_socket(PORT);
    if (listen_sock == -1) {
        exit(EXIT_FAILURE);
    }

    // Create an epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // Add the listening socket to the epoll instance
    event.events = EPOLLIN | EPOLLET; // Watch for new connections (input), edge-triggered
    event.data.fd = listen_sock; // Associate this event with the listening socket's FD
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        perror("epoll_ctl: adding listen_sock failed");
        close(listen_sock);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // Main event loop
    // This loop continuously waits for and processes I/O events.
    // --- MODIFIED: Loop condition now checks the 'running' flag ---
    while (running) {
        // Wait for events on the epoll instance.
        // We use a short timeout (e.g., 1000ms = 1 second) so the loop
        // can periodically check for Keep-Alive timeouts even if no I/O events occur.
        num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // Wait for up to 1 second
        if (num_events == -1) {
            // If epoll_wait was interrupted by a signal (e.g., by SIGINT), it will return -1 with EINTR.
            // In this case, we check the 'running' flag. If it's 0, we exit gracefully.
            if (errno == EINTR) {
                if (!running) { // If SIGINT set running to 0, break the loop
                    break;
                }
                continue; // Otherwise, just continue waiting
            }
            perror("epoll_wait failed");
            // If a non-EINTR error occurs, it's a serious problem, exit.
            running = 0; // Ensure loop terminates
            continue; // Go to cleanup
        }

        // Process all I/O events that occurred
        for (i = 0; i < num_events; i++) {
            // Check if the event is on the listening socket.
            // This indicates a new incoming connection is ready to be accepted.
            if (events[i].data.fd == listen_sock) {
                handle_new_connection(listen_sock, epoll_fd);
            } else {
                // Event on a client socket
                // Pass the entire epoll_event structure, as it contains our client_state_t pointer.
                handle_client_event(epoll_fd, &events[i]);
            }
        }

        // Check for Keep-Alive timeouts after processing I/O events
        // Iterate through all possible file descriptors to find active clients.
        time_t current_time = time(NULL);
        for (i = 0; i < FD_SETSIZE; i++) {
            client_state_t *client_state = client_states_map[i];
            if (client_state != NULL && client_state->keep_alive) {
                // If the client is in READING_REQUEST state and has been idle for too long
                if (client_state->state == READING_REQUEST &&
                    (current_time - client_state->last_activity_time) > KEEPALIVE_IDLE_TIMEOUT_SECONDS) {
                    printf("Client socket %d (Keep-Alive) timed out after %d seconds idle. Closing.\n",
                           client_state->fd, KEEPALIVE_IDLE_TIMEOUT_SECONDS);
                    close_client_connection(epoll_fd, client_state);
                }
            }
        }
    }

    // --- NEW: Graceful Cleanup Section ---
    printf("Server shutting down. Cleaning up resources...\n");

    // Close the listening socket
    close(listen_sock);

    // Close and free all active client connections
    for (i = 0; i < FD_SETSIZE; i++) {
        client_state_t *client_state = client_states_map[i];
        if (client_state != NULL) {
            printf("Closing active client socket %d during shutdown.\n", client_state->fd);
            // close_client_connection handles epoll_ctl_del, close, and free
            close_client_connection(epoll_fd, client_state);
        }
    }

    // Close the epoll instance file descriptor
    close(epoll_fd);
    printf("Server shutdown complete.\n");

    return 0;
}
