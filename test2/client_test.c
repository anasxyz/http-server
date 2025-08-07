#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/wait.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define NUM_CLIENTS 10
char* REQUEST = "POST / HTTP/1.1\r\n"
								"Host: localhost:8080\r\n"
								"Connection: keep-alive\r\n"
								"Content-Type: text/plain\r\n"
								"Content-Length: 18\r\n"
								"\r\n\r\n"	
								"This is the body!";

void run_client(int client_id) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sock);
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(sock);
        return;
    }

    printf("Client %d connected to server. Sending request...\n", client_id);
    send(sock, REQUEST, strlen(REQUEST), 0);

    // Read the server's response
    ssize_t valread = read(sock, buffer, 1024);
    if (valread > 0) {
        printf("Client %d received response. Now waiting for timeout...\n", client_id);
    }

    // Loop until the server closes the connection due to timeout
    while (1) {
        // We'll try to read again. A return value of 0 means the server closed the connection.
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        tv.tv_sec = 1; // Check every second
        tv.tv_usec = 0;

        int ret = select(sock + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            valread = read(sock, buffer, 1024);
            if (valread == 0) {
                printf("Client %d detected server closed connection. Exiting.\n", client_id);
                break;
            } else if (valread < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("Read error");
                    break;
                }
            }
        }
        // If select times out, we just loop again and keep waiting.
    }

    close(sock);
}

int main() {
    // Create multiple clients in separate processes
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            run_client(i);
            exit(0);
        } else if (pid < 0) {
            perror("fork failed");
        }
    }

    // Wait for all child processes to finish
    for (int i = 0; i < NUM_CLIENTS; i++) {
        wait(NULL);
    }
    printf("All clients have finished.\n");

    return 0;
}
