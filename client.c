#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h"
#include "http.h"

void *handle_client(void *arg) {
  struct Client *client = (struct Client *)arg;

  char buffer[30000];
  memset(buffer, 0, sizeof(buffer));

  read(client->socket, buffer, sizeof(buffer) - 1);

  printf("Received request: %s\n", buffer);

  handle_request(client->socket, buffer);
  close(client->socket);

  free(client);
  return NULL;
}
