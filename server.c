#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit
#include <string.h>     // For string manipulation like memset
#include <unistd.h>     // For close function, read, write
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

        // A new connection has been successfully accepted!
        printf("New connection accepted on socket %d\n", conn_sock);

        // Set the newly accepted client socket to non-blocking mode.
        // This ensures that future read/write operations on this client
        // socket won't block the entire server.
        if (set_nonblocking(conn_sock) == -1) {
            close(conn_sock); // Close the problematic socket
            continue;         // Skip to the next potential connection
        }

        // Add the new client socket to the epoll instance.
        // We want to be notified when this client socket has data to read (EPOLLIN)
        // AND when it's ready for writing (EPOLLOUT).
        event.events = EPOLLIN | EPOLLOUT | EPOLLET; // Watch for input/output, edge-triggered
        event.data.fd = conn_sock;        // Associate with the client socket's FD
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
            perror("epoll_ctl: adding conn_sock failed");
            close(conn_sock); // Close the problematic socket
            continue;         // Skip to the next potential connection
        }
    }
}

// Function to handle events on an existing client socket.
// It reads all available data or detects disconnection/errors, and now sends a simple HTTP response.
// 'current_fd' is the client socket file descriptor.
// 'event_flags' are the specific events reported by epoll_wait for this FD.
void handle_client_event(int current_fd, int epoll_fd, uint32_t event_flags) {
    char buffer[1024];
    ssize_t bytes_read;
    ssize_t bytes_sent;
    int should_close = 0; // Flag to indicate if the socket should be closed

    // Check for connection closed by peer (EPOLLRDHUP), hang up (EPOLLHUP), or error (EPOLLERR)
    // These flags indicate the client has disconnected or an error occurred.
    if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        printf("Client socket %d disconnected or error occurred.\n", current_fd);
        should_close = 1; // Mark for closure
    }
    // Check for data available to read (EPOLLIN)
    // This condition is only checked if the socket is not already marked for closure by an error/disconnect event.
    else if (event_flags & EPOLLIN) {
        // In edge-triggered mode, it's crucial to read ALL available data
        // until read() returns EAGAIN/EWOULDBLOCK or 0 (EOF).
        while (1) {
            bytes_read = read(current_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data to read for now. Connection is still open.
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

                // Send a simple HTTP response immediately after receiving data.
                // For this simple example, we assume the response is small enough
                // to be sent in one go without blocking.
                const char *http_response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: 12\r\n" // Length of "Hello World!"
                    "\r\n"
                    "Hello World!";

                bytes_sent = write(current_fd, http_response, strlen(http_response));
                if (bytes_sent == -1) {
                    perror("write to client socket failed");
                    should_close = 1; // Mark for closure on write error
                } else {
                    printf("Sent %zd bytes to client %d.\n", bytes_sent, current_fd);
                }

                // After sending the response, for this simple server, we assume
                // the interaction is complete and close the connection.
                should_close = 1; // Mark for closure after sending response
                break; // Exit read loop after handling the request
            }
        }
    }
    // Handle EPOLLOUT event: This indicates the socket is ready for writing.
    // This block would be used in a more complex server if a previous write()
    // call returned EAGAIN/EWOULDBLOCK (send buffer full). In that scenario,
    // you would have buffered data and would attempt to send it here.
    // For this simple example, our response is small and sent immediately upon EPOLLIN,
    // so this block primarily serves as a demonstration of where EPOLLOUT would be handled.
    else if (event_flags & EPOLLOUT) {
        printf("Client socket %d is ready for writing (EPOLLOUT).\n", current_fd);
        // In a real server, you would now attempt to send any pending buffered data.
        // Once all pending data is sent, you would typically modify the epoll interest
        // for this socket to remove EPOLLOUT (using epoll_ctl with EPOLL_CTL_MOD).
    }

    // If the should_close flag is set, close the socket and remove it from epoll
    if (should_close) {
        // Attempt to remove the file descriptor from the epoll instance FIRST.
        // Check for ENOENT (No such file or directory) which means the FD was
        // already implicitly removed or never added, which is not a critical error here.
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 && errno != ENOENT) {
             perror("epoll_ctl: DEL client socket failed"); // Only print if it's a real error
        }
        close(current_fd); // Now close the socket itself.
        printf("Client socket %d fully closed and removed from epoll.\n", current_fd);
    }
}


int main() {
    int listen_sock;            // The socket that listens for new incoming connections
    int epoll_fd;               // The file descriptor for our epoll instance
    struct epoll_event event;   // A single epoll_event structure to configure events
    struct epoll_event events[MAX_EVENTS]; // Array to store events returned by epoll_wait
    int num_events;             // Number of events returned by epoll_wait
    int i;                      // Loop counter for processing events array

    printf("Starting simple epoll server on port %d.\n", PORT);

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
    while (1) {
        // Wait for events on the epoll instance
        // -1: Timeout in milliseconds. -1 means wait indefinitely until an event occurs.
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

        // Process all events that occurred
        for (i = 0; i < num_events; i++) {
            // Check if the event is on the listening socket.
            // This indicates a new incoming connection is ready to be accepted.
            if (events[i].data.fd == listen_sock) {
                handle_new_connection(listen_sock, epoll_fd);
            } else {
                // Event on a client socket
                // Pass the client's file descriptor and the specific event flags that occurred.
                handle_client_event(events[i].data.fd, epoll_fd, events[i].events);
            }
        }
    }

    // Cleanup (unreachable in this infinite loop, but good practice)
    close(listen_sock);
    close(epoll_fd);

    return 0;
}
