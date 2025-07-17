#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/config.h"

#include "../include/server.h"
#include "../include/client.h"

void launch(struct Server *server) {
  printf("===== WAITING FOR CONNECTION =====\n");

  // infinite loop accepting connections
  while (1) {
    int address_length = sizeof(server->address);
    int new_socket = accept(server->socket, (struct sockaddr *)&server->address,
                            (socklen_t *)&address_length);

    if (new_socket < 0) {
      perror("Failed to accept connection...\n");
      continue; 
    }

    struct Client *client = malloc(sizeof(struct Client));
    if (!client) {
      perror("Failed to allocate memory for client...\n");
      close(new_socket);
      continue;
    }
    client->socket = new_socket;

    pthread_t tid;
    pthread_create(&tid, NULL, handle_client, client);
    pthread_detach(tid);
  }
}

int main() {
  load_config("server.conf");

  struct Server server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, 8080, 10, launch);

  server.launch(&server);
}
