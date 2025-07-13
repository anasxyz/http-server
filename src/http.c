#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/http.h"
#include "../include/file_handler.h"

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

HttpRequest parse_request(char *request_buffer) {
  HttpRequest request = {
    .method = NULL,
    .path = NULL,
    .version = NULL,
  };

  // extract request line
  char *request_line = strtok(request_buffer, "\r\n");
  if (request_line) { 
    request.method = strtok(request_line, " ");
    request.path = strtok(NULL, " ");
    request.version = strtok(NULL, " ");
  }

  // if request_line is NULL (extraction failed), HttpRequest fields stay NULL
  // and then handle_request() can check for errors

  return request;
}

// handles HTTP request
void handle_request(int socket, char *request_buffer) {
  HttpRequest request = parse_request(request_buffer);

  if (!request.method || !request.path || !request.version) {
    char *message = "Bad Request";

    HttpResponse response = {
      .status = "400 Bad Request",
      .content_type = "text/plain",
      .body = message,
      .body_length = strlen(message),
      .headers = NULL,
      .num_headers = 0,
    };

    send_response(socket, &response);
  }

  // only support GET requests for now
  if (strcmp(request.method, "GET") != 0) {
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
  if (strstr(request.path, "..")) {
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
  if (strcmp(request.path, "/") == 0) {
    snprintf(filepath, sizeof(filepath), "www/index.html");
  } else {
    if (request.path[0] != '/') { request.path++; }
    snprintf(filepath, sizeof(filepath), "www/%s", request.path);
  }

  serve_file(socket, filepath);
}
