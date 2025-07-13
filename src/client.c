#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/client.h"
#include "../include/http.h"

void *handle_client(void *arg) {
  struct Client *client = (struct Client *)arg;

  char request_buffer[30000];
  memset(request_buffer, 0, sizeof(request_buffer));

  read(client->socket, request_buffer, sizeof(request_buffer) - 1);

  printf("Received request: %s\n", request_buffer);

  handle_request(client->socket, request_buffer);
  close(client->socket);

  free(client);
  return NULL;
}
