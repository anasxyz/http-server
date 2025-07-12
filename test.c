#include "server.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void launch(struct Server *server) {
  char buffer[30000];

  printf("===== WAITING FOR CONNECTION =====\n");

  int address_length = sizeof(server->address);
  int new_socket = accept(server->socket, (struct sockaddr *)&server->address, (socklen_t *)&address_length);

  read(new_socket, buffer, 30000);

  printf("%s\n", buffer);

  char *hello = "HTTP/1.1 200 OK\nDate: Sat, 12 Jul 2025 14:00:00 GMT\nServer: Apache/2.2.14 (Win32)\nLast-Modified: Wed, 22 Jul 2009 19:15:56 GMT\nContent-Length: 88\nContent-Type: text/html\nConnection: Closed\n<html><body><h1>Homo Deus</h1></body></html>";

  write(new_socket, hello, strlen(hello));
  
  close(new_socket);
}

int main() {
  struct Server server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, 8080, 10, launch);

  server.launch(&server);
}
