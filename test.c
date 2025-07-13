#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

struct Client {
  int socket;
};

const char *get_mime_type(const char *path) {
  const char *extension = strrchr(path, '.');

  if (extension == NULL) {
    return "application/octet-stream";
  }

  if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) { return "text/html"; }
  if (strcmp(extension, ".css") == 0) { return "text/css"; }
  if (strcmp(extension, ".js") == 0) { return "text/javascript"; }
  if (strcmp(extension, ".json") == 0) { return "application/json"; }
  if (strcmp(extension, ".png") == 0) { return "image/png"; }
  if (strcmp(extension, ".jpg") == 0) { return "image/jpeg"; }
  if (strcmp(extension, ".gif") == 0) { return "image/gif"; }
  if (strcmp(extension, ".txt") == 0) { return "text/plain"; }

  return "application/octet-stream"; // default MIME type for unknown extensions
}

// sends HTTP response
void send_response(int socket, const char *status, const char *content_type, const char *body) {
  char header[512];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %lu\r\n"
           "Connection: close\r\n"
           "\r\n",
           status, content_type, strlen(body));

  write(socket, header, strlen(header));
  write(socket, body, strlen(body));
}

// reads and serves requested file
void serve_file(int socket, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    // 404 not found
    char *not_found_body = "<html><body><h1>404 Not Found</h1></body></html>";
    // not using get_mime_type() here just because we don't know the file extension
    send_response(socket, "404 not found", "text/html", not_found_body); 
    return;
  }

  // get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // read file into buffer
  char *file_buffer = malloc(file_size);
  if (!file_buffer) {
    fclose(file);
    return;
  }

  fread(file_buffer, 1, file_size, file);
  fclose(file);

  // build response header
  const char *mime_type = get_mime_type(path);
  send_response(socket, "200 OK", mime_type, file_buffer);

  // free memory
  free(file_buffer);
}

// handles HTTP request
void handle_request(int socket, char *request) {
  // parse request - extract request line
  char *request_line = strtok(request, "\r\n");
  if (!request_line) { return; }

  // parse request line - extract method, path, and version from request line
  char *method = strtok(request_line, " ");
  char *path = strtok(NULL, " ");
  char *version = strtok(NULL, " ");

  if (!method || !path || !version) { return; }

  // only support GET requests for now
  if (strcmp(method, "GET") != 0) {
    const char *message = "Method not allowed";
    send_response(socket, "405 Method Not Allowed", "text/plain", message);
    return;
  }

  // map URL path to file system path
  char filepath[512];

  if (strstr(path, "..")) {
    const char *message = "Bad request";
    send_response(socket, "400 Bad Request", "text/plain", message);
    return;
  }

  if (strcmp(path, "/") == 0) {
    snprintf(filepath, sizeof(filepath), "www/index.html");
  } else {
    snprintf(filepath, sizeof(filepath), "www/%s", path);
  }

  serve_file(socket, filepath);
}

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
  struct Server server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, 8080, 10, launch);

  server.launch(&server);
}
