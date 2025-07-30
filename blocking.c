#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // For read, sleep

int main() {
    char buffer[256];
    ssize_t bytes_read;
    int counter = 0;

    printf("Starting blocking input example with background task.\n");
    printf("Notice how the background task stops when waiting for input.\n");
    printf("Type something and press Enter.\n");
    printf("Press Ctrl+D (EOF) to exit.\n");

    while (1) {
        printf("Background task working... (%d)\n", counter++);
        fflush(stdout); // Ensure printf output is shown immediately
        sleep(1);       // Simulate some work

        printf("Waiting for input... (Program will block here)\n");
        // This read call will BLOCK the program until data is available
        bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);

        if (bytes_read == -1) {
            perror("read STDIN_FILENO");
            exit(EXIT_FAILURE);
        } else if (bytes_read == 0) {
            printf("EOF detected on stdin. Exiting.\n");
            break;
        } else {
            buffer[bytes_read] = '\0';
            printf("Read: '%s' (%zd bytes)\n", buffer, bytes_read);
        }
    }

    printf("Blocking example finished.\n");
    return 0;
}

