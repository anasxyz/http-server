#include "server.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

const char *get_mime_type(const char *path) {
  const char *extension = strrchr(path, '.');

  if (extension == NULL) { return "application/octet-stream"; }

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

void launch(struct Server *server) {
  char buffer[30000];

  printf("===== WAITING FOR CONNECTION =====\n");

  // infinite loop accepting connections
  while (1) {
    int address_length = sizeof(server->address);
    int new_socket = accept(server->socket, (struct sockaddr *)&server->address, (socklen_t *)&address_length);

    if (new_socket < 0) {
      perror("Failed to accept connection...\n");
      exit(1);
    }

    memset(buffer, 0, sizeof(buffer));
    read(new_socket, buffer, 30000);

    printf("Received request: %s\n", buffer);

    /* char *response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: 51\r\n"
      "Connection: close\r\n"
      "\r\n"
      "<html><body><h1>Hello</h1></body></html>";
    */
    
    // parse request - extract request line
    char *request_line = strtok(buffer, "\r\n");
    if (!request_line) { 
      close(new_socket); 
      continue; 
    }
  
    // parse request line - extract method, path, and version from request line
    char *method = strtok(request_line, " ");
    char *path = strtok(NULL, " ");
    char *version = strtok(NULL, " ");

    if (!method || !path || !version) { 
      close(new_socket); 
      continue; 
    }

    // only support GET requests for now
    if (strcmp(method, "GET") != 0) {
      char *response =
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
      write(new_socket, response, strlen(response));
      close(new_socket);
      continue;
    }

    // map URL path to file system path
    char filepath[512];
    if (strcmp(path, "/") == 0) {
      snprintf(filepath, sizeof(filepath), "www/index.html");
    } else {
      // prevent directory traversal attacks by not allowing ".."
      if (strstr(path, "..")) {
        char *response =
          "HTTP/1.1 400 Bad Request\r\n"
          "Content-Length: 0\r\n"
          "Connection: close\r\n"
          "\r\n";
        write(new_socket, response, strlen(response));
        close(new_socket);
        continue;
      }

      snprintf(filepath, sizeof(filepath), "www/%s", path);
    }

    // open file
    FILE *file = fopen(filepath, "rb");
    if (!file) {
      // 404 not found
      char *not_found_body = "<html><body><h1>404 Not Found</h1></body></html>";
      char header[512];
      snprintf(header, sizeof(header),
               "HTTP/1.1 404 Not Found\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: %lu\r\n"
               "Connection: close\r\n"
               "\r\n",
               strlen(not_found_body));

      write(new_socket, header, strlen(header));
      write(new_socket, not_found_body, strlen(not_found_body));
      close(new_socket);
      continue;
    }

    // get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // read file into buffer
    char *file_buffer = malloc(file_size);
    if (!file_buffer) {
      fclose(file);
      close(new_socket);
      continue;
    }
    fread(file_buffer, 1, file_size, file);
    fclose(file);

    // build response header
    const char *mime_type = get_mime_type(filepath);
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %lu\r\n"
             "Connection: close\r\n"
             "\r\n",
             mime_type, file_size);

    // send header + file content
    write(new_socket, header, strlen(header));
    write(new_socket, file_buffer, file_size);

    // free memory
    free(file_buffer);
    close(new_socket);
  }
}

int main() {
  struct Server server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, 8080, 10, launch);

  server.launch(&server);
}
