#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define NUM_CLIENTS 100
char *REQUEST = "POST / HTTP/1.1\r\n"
                "Host: localhost:8080\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 18\r\n"
                "\r\n\r\n"
                "This is the body!";

void run_client(int client_id) {
  int sock = 0;
  struct sockaddr_in serv_addr;

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

  // --- START of modified code block ---
  char *response_buffer = NULL;
  size_t buffer_size = 0;
  ssize_t valread;
  char temp_buffer[1024];

  while ((valread = read(sock, temp_buffer, sizeof(temp_buffer))) > 0) {
    response_buffer = realloc(response_buffer, buffer_size + valread);
    if (response_buffer == NULL) {
      perror("realloc failed");
      free(response_buffer);
      close(sock);
      return;
    }
    memcpy(response_buffer + buffer_size, temp_buffer, valread);
    buffer_size += valread;
  }

  // Null-terminate the full buffer before printing
  if (response_buffer != NULL) {
    response_buffer = realloc(response_buffer, buffer_size + 1);
    response_buffer[buffer_size] = '\0';
    printf("Client %d received:\n%s", client_id, response_buffer);
  }

  if (valread == 0) {
    printf("\nClient %d: server closed the connection.\n", client_id);
  } else if (valread < 0) {
    perror("Read error");
  }

  free(response_buffer); // Free the allocated memory
  // --- END of modified code block ---

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
