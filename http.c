#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"
#include "file_handler.h"

// sends HTTP response
void send_response(int socket, HttpResponse *response) {
  char header[512];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %lu\r\n"
           "Connection: close\r\n"
           "\r\n",
           response->status,
           response->content_type,
           response->body_length);

  // TODO: explore possibility of extra headers in the future

  write(socket, header, strlen(header));
  write(socket, response->body, response->body_length);
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
    char *message = "Method not allowed";
  
    HttpResponse response = {
      .status = "405 Method Not Allowed",
      .content_type = "text/plain",
      .body = message,
      .body_length = strlen(message),
      .headers = NULL,
      .num_headers = 0,
    };
    
    send_response(socket, &response);

    return;
  }

  // reject path with ".." in it
  if (strstr(path, "..")) {
    char *message = "Bad request";

    HttpResponse response = {
      .status = "400 Bad Request",
      .content_type = "text/plain",
      .body = message,
      .body_length = strlen(message),
      .headers = NULL,
      .num_headers = 0,
    };

    send_response(socket, &response);
    return;
  }

  // map URL path to file system path
  char filepath[512];
  if (strcmp(path, "/") == 0) {
    snprintf(filepath, sizeof(filepath), "www/index.html");
  } else {
    if (path[0] != '/') { path++; }
    snprintf(filepath, sizeof(filepath), "www/%s", path);
  }

  serve_file(socket, filepath);
}
