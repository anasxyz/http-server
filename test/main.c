#include <stdio.h>      // For printf, perror
#include <stdlib.h>     // For exit
#include <unistd.h>     // For close, read
#include <sys/epoll.h>  // For epoll functions
#include <fcntl.h>      // For fcntl (to set non-blocking)
#include <errno.h>      // For errno and EAGAIN/EWOULDBLOCK

// Function to set a file descriptor to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0; // Success
}

int main() {
    int epoll_fd;
    struct epoll_event event;
    struct epoll_event events[1]; // We only expect one event at a time for simplicity
    char buffer[256];
    ssize_t bytes_read;

    printf("Starting simplest epoll example.\n");
    printf("Type something and press Enter to trigger an event.\n");
    printf("Press Ctrl+D (EOF) to exit.\n");

    // 1. Create an epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // 2. Set standard input (stdin, file descriptor 0) to non-blocking mode
    if (set_nonblocking(STDIN_FILENO) == -1) {
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Add stdin to the epoll instance
    // EPOLLIN: We want to know when there's data to read
    // EPOLLET: Edge-triggered mode (report only when new data arrives)
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = STDIN_FILENO; // Associate with standard input
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) {
        perror("epoll_ctl: STDIN_FILENO");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // Main event loop
    while (1) {
        // 4. Wait for events
        // -1 means wait indefinitely until an event occurs
        int num_events = epoll_wait(epoll_fd, events, 1, -1);
        if (num_events == -1) {
            if (errno == EINTR) { // Handle interrupted system call
                continue;
            }
            perror("epoll_wait");
            close(epoll_fd);
            exit(EXIT_FAILURE);
        }

        // We only asked for 1 event, so we just check events[0]
        if (num_events > 0 && events[0].data.fd == STDIN_FILENO) {
            printf("\nEvent detected on stdin! Reading data...\n");
            // In edge-triggered mode, we must read ALL available data
            // until read() returns EAGAIN/EWOULDBLOCK or 0 (EOF)
            while (1) {
                bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
                if (bytes_read == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // No more data to read for now
                        printf("No more data from stdin (EAGAIN/EWOULDBLOCK).\n");
                        break;
                    } else {
                        perror("read STDIN_FILENO");
                        close(epoll_fd);
                        exit(EXIT_FAILURE);
                    }
                } else if (bytes_read == 0) {
                    // End of file (Ctrl+D)
                    printf("EOF detected on stdin. Exiting.\n");
                    goto cleanup; // Jump to cleanup section
                } else {
                    // Successfully read some data
                    buffer[bytes_read] = '\0'; // Null-terminate the string
                    printf("Read: '%s' (%zd bytes)\n", buffer, bytes_read);
                }
            }
            printf("Waiting for next input...\n");
        }
    }

cleanup:
    // Clean up
    close(epoll_fd);
    printf("Epoll example finished.\n");

    return 0;
}

