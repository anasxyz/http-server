#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit
#include <string.h>     // For string manipulation like memset
#include <unistd.h>     // For close function, read
#include <sys/socket.h> // For socket, bind, listen, accept functions
#include <netinet/in.h> // For sockaddr_in structure
#include <fcntl.h>      // For fcntl (file control) to set non-blocking mode
#include <sys/epoll.h>  // For epoll functions
#include <errno.h>      // For errno and EWOULDBLOCK/EAGAIN

// Define the port number for our server
#define PORT 8080
// Define the maximum number of events epoll_wait can return at once
#define MAX_EVENTS 10

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

int main() {
    int listen_sock;            // The socket that listens for new incoming connections
    int conn_sock;              // A socket for an accepted client connection
    int epoll_fd;               // The file descriptor for our epoll instance
    struct sockaddr_in server_addr; // Structure to hold server address information
    struct sockaddr_in client_addr; // Structure to hold client address information
    socklen_t client_len;       // Size of the client address structure
    struct epoll_event event;   // A single epoll_event structure to configure events
    struct epoll_event events[MAX_EVENTS]; // Array to store events returned by epoll_wait
    int num_events;             // Number of events returned by epoll_wait
    int i;                      // Loop counter

    printf("Starting simple epoll server on port %d.\n", PORT);

    // 1. Create a listening socket
    // AF_INET: Use IPv4 addresses
    // SOCK_STREAM: Use TCP (reliable, connection-oriented)
    // 0: Let the system choose the appropriate protocol (TCP for SOCK_STREAM)
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Optional: Set SO_REUSEADDR to allow immediate reuse of the port
    // This is useful during development to avoid "Address already in use" errors
    // if the server is restarted quickly.
    int opt = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // Prepare the server address structure
    memset(&server_addr, 0, sizeof(server_addr)); // Clear the structure to zeros
    server_addr.sin_family = AF_INET;             // IPv4 address family
    server_addr.sin_addr.s_addr = INADDR_ANY;     // Listen on all available network interfaces
    server_addr.sin_port = htons(PORT);           // Convert port number to network byte order

    // 2. Bind the listening socket to the specified IP address and port
    // This assigns the address to the socket.
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // 3. Start listening for incoming connections
    // 10: The maximum length of the queue of pending connections.
    if (listen(listen_sock, 10) == -1) {
        perror("listen failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening for connections...\n");

    // 4. Set the listening socket to non-blocking mode
    // This is crucial for epoll. 'accept' on this socket won't block the server.
    if (set_nonblocking(listen_sock) == -1) {
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // 5. Create an epoll instance
    // epoll_create1(0) is the modern way to create an epoll file descriptor.
    // It returns a file descriptor that refers to the new epoll instance.
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1 failed");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // 6. Add the listening socket to the epoll instance
    // We tell epoll to watch for events on our listening socket.
    // EPOLLIN: We are interested when there is data to read (or a new connection to accept).
    // EPOLLET: Edge-triggered mode. This means epoll will notify us *only once*
    //          when a new event occurs (e.g., a new connection arrives).
    //          It won't keep notifying us if the condition still holds.
    //          This requires us to handle *all* pending events (e.g., accept all
    //          pending connections) when notified.
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listen_sock; // Associate this event with the listening socket's FD

    // EPOLL_CTL_ADD: Add the file descriptor to the epoll interest list.
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        perror("epoll_ctl: adding listen_sock failed");
        close(listen_sock);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // Main event loop
    // This loop continuously waits for and processes I/O events.
    while (1) {
        // 7. Wait for events on the epoll instance
        // epoll_fd: The epoll instance file descriptor.
        // events: Array where epoll_wait will store the events that occurred.
        // MAX_EVENTS: The maximum number of events to return in one call.
        // -1: Timeout in milliseconds. -1 means wait indefinitely until an event occurs.
        //     A positive value would be a timeout (e.g., 1000 for 1 second).
        //     For a server, waiting indefinitely is common as it only needs to react to events.
        num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            // If epoll_wait was interrupted by a signal (e.g., Ctrl+C), just continue.
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait failed");
            close(listen_sock);
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }

        // 8. Process all events that occurred
        for (i = 0; i < num_events; i++) {
            // Check if the event is on the listening socket.
            // This indicates a new incoming connection is ready to be accepted.
            if (events[i].data.fd == listen_sock) {
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
                            // printf("No more pending connections.\n"); // Uncomment for debugging
                            break; // Exit the accept loop
                        } else {
                            perror("accept failed");
                            // In a real server, you might log this error and continue,
                            // rather than exiting, to keep the server running.
                            close(listen_sock);
                            close(epoll_fd);
                            exit(EXIT_FAILURE);
                        }
                    }

                    // A new connection has been successfully accepted!
                    printf("New connection accepted on socket %d\n", conn_sock);

                    // Set the newly accepted client socket to non-blocking mode.
                    // This ensures that future read/write operations on this client
                    // socket won't block the entire server.
                    if (set_nonblocking(conn_sock) == -1) {
                        close(conn_sock); // Close the problematic socket
                        continue;         // Skip to the next event/connection
                    }

                    // Add the new client socket to the epoll instance.
                    // We want to be notified when this client socket has data to read.
                    event.events = EPOLLIN | EPOLLET; // Watch for input, edge-triggered
                    event.data.fd = conn_sock;        // Associate with the client socket's FD
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
                        perror("epoll_ctl: adding conn_sock failed");
                        close(conn_sock); // Close the problematic socket
                        continue;         // Skip to the next event/connection
                    }
                }
            } else {
                // This block handles events on *client* sockets (not the listening socket).
                int current_fd = events[i].data.fd;
                char buffer[1024];
                ssize_t bytes_read;
                int should_close = 0; // Flag to indicate if the socket should be closed

                // Check for connection closed by peer (EPOLLRDHUP), hang up (EPOLLHUP), or error (EPOLLERR)
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    printf("Client socket %d disconnected or error occurred.\n", current_fd);
                    should_close = 1; // Mark for closure
                }
                // Check for data available to read (EPOLLIN)
                else if (events[i].events & EPOLLIN) {
                    // In edge-triggered mode, it's crucial to read ALL available data
                    // until read() returns EAGAIN/EWOULDBLOCK or 0 (EOF).
                    while (1) {
                        bytes_read = read(current_fd, buffer, sizeof(buffer) - 1);
                        if (bytes_read == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // No more data to read for now. Connection is still open.
                                // printf("No more data from client %d for now.\n", current_fd); // For debugging
                                break; // Exit read loop, wait for next event
                            } else {
                                perror("read client socket failed");
                                should_close = 1; // Fatal read error, mark for closure
                                break;
                            }
                        } else if (bytes_read == 0) {
                            // Client closed their side of the connection (End-of-File)
                            printf("Client socket %d closed connection.\n", current_fd);
                            should_close = 1; // Mark for closure
                            break; // Exit read loop
                        } else {
                            // Successfully read some data
                            buffer[bytes_read] = '\0'; // Null-terminate the received data
                            printf("Received from client %d: '%s'\n", current_fd, buffer);
                            // In a real HTTP server, you would parse this request,
                            // process it, and prepare a response here.
                            // For now, we just read and print.
                        }
                    }
                }

                // If the should_close flag is set, close the socket and remove it from epoll
                if (should_close) {
                    close(current_fd);
                    // Remove the file descriptor from the epoll instance.
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL);
                    printf("Client socket %d fully closed and removed from epoll.\n", current_fd);
                }
            }
        }
    }

    // Cleanup (these lines are technically unreachable in the infinite loop,
    // but good practice for proper resource management if the loop were to exit).
    close(listen_sock);
    close(epoll_fd);

    return 0;
}
