#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define NUM_CLIENTS 1
char* REQUEST = "POS / HTTP/1.1\r\n"
								"Host: localhost:8080\r\n"
								"Connection: keep-alive\r\n"
								"Content-Type: text/plain\r\n"
								"Content-Length: 18\r\n"
								"\r\n\r\n"	
								"This is the body!";

void run_client(int client_id) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024];

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

    printf("Client %d waiting for server response...\n", client_id);

    // Read and print the server's response
    ssize_t valread;
    while ((valread = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[valread] = '\0';  // Null-terminate
        printf("Client %d received:\n%s", client_id, buffer);
    }

    if (valread == 0) {
        printf("\nClient %d: server closed the connection.\n", client_id);
    } else if (valread < 0) {
        perror("Read error");
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
