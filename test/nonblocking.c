#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

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
    struct epoll_event events[1];
    char buffer[256];
    ssize_t bytes_read;
    int counter = 0;

    printf("Starting epoll example with background task.\n");
    printf("Notice how the background task continues while waiting for input.\n");
    printf("Type something and press Enter to trigger an event.\n");
    printf("Press Ctrl+D (EOF) to exit.\n");

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    if (set_nonblocking(STDIN_FILENO) == -1) {
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    event.events = EPOLLIN | EPOLLET;
    event.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) {
        perror("epoll_ctl: STDIN_FILENO");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        // We use a timeout of 1000ms (1 second) for epoll_wait
        // This allows epoll_wait to return even if no events,
        // so we can do our "background task".
        int num_events = epoll_wait(epoll_fd, events, 1, 1000); // Wait for 1 second

        if (num_events == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            close(epoll_fd);
            exit(EXIT_FAILURE);
        } else if (num_events == 0) {
            // No events occurred within the 1-second timeout
            // This is where we can do our "background task"
            printf("Background task working... (%d)\n", counter++);
            fflush(stdout); // Ensure printf output is shown immediately
            // No sleep(1) here, as epoll_wait already handles the delay
            continue; // Go back to waiting for events
        }

        // If num_events > 0, an event occurred
        if (events[0].data.fd == STDIN_FILENO) {
            printf("\nEvent detected on stdin! Reading data...\n");
            while (1) {
                bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
                if (bytes_read == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("No more data from stdin (EAGAIN/EWOULDBLOCK).\n");
                        break;
                    } else {
                        perror("read STDIN_FILENO");
                        close(epoll_fd);
                        exit(EXIT_FAILURE);
                    }
                } else if (bytes_read == 0) {
                    printf("EOF detected on stdin. Exiting.\n");
                    goto cleanup;
                } else {
                    buffer[bytes_read] = '\0';
                    printf("Read: '%s' (%zd bytes)\n", buffer, bytes_read);
                }
            }
        }
    }

cleanup:
    close(epoll_fd);
    printf("Epoll example finished.\n");

    return 0;
}

