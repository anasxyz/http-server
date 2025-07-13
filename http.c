#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"
#include "file_handler.h"

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
