#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/proxy.h"
#include "../include/utils_http.h"

HttpResponse *proxy_to_backend(HttpRequest request) {
  // backend socket
  int backend_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (backend_socket < 0) {
    perror("Failed to create backend socket");
    return NULL;
  }

  // backend server address
  struct sockaddr_in backend_addr;
  memset(&backend_addr, 0, sizeof(backend_addr));
  backend_addr.sin_family = AF_INET;           // IPv4
  backend_addr.sin_port = htons(BACKEND_PORT); // set port

  // convert backend address to network byte order
  if (inet_pton(AF_INET, BACKEND_HOST, &backend_addr.sin_addr) <= 0) {
    perror("Invalid backend address");
    close(backend_socket);
    return NULL;
  }

  // try to connect
  if (connect(backend_socket, (struct sockaddr *)&backend_addr,
              sizeof(backend_addr)) < 0) {
    perror("Failed to connect to backend");
    close(backend_socket);
    return NULL;
  }

  // build HTTP request to send to backend
  char request_buffer[2048];
  snprintf(request_buffer, sizeof(request_buffer),
           "%s %s HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "Connection: close\r\n"
           "\r\n",
           request.method, request.path, BACKEND_HOST, BACKEND_PORT);

  // send request to backend
  if (write(backend_socket, request_buffer, strlen(request_buffer)) < 0) {
    perror("Failed to write to backend");
    close(backend_socket);
    return NULL;
  }

  // read response from backend into a buffer
  char response_buffer[8192];
  ssize_t bytes_received =
      read(backend_socket, response_buffer, sizeof(response_buffer) - 1);
  if (bytes_received < 0) {
    perror("Failed to read from backend");
    close(backend_socket);
    return NULL;
  }
  response_buffer[bytes_received] = '\0'; // null terminate response buffer to use as string
  close(backend_socket); // no longer need socket

  // parse status line
  char *status_line_end = strstr(response_buffer, "\r\n");
  if (!status_line_end) {
    fprintf(stderr, "Malformed response from backend\n");
    return NULL;
  }

  size_t status_line_len = status_line_end - response_buffer;
  char *status_line = strndup(response_buffer, status_line_len); // copy just status line

  // find start of body (after \r\n\r\n)
  char *body_start = strstr(response_buffer, "\r\n\r\n");
  if (!body_start) {
    fprintf(stderr, "No body in backend response\n");
    free(status_line);
    return NULL;
  }
  body_start += 4; // skip \r\n\r\n
  
  size_t body_len = bytes_received - (body_start - response_buffer);

  // allocate memory to copy body into
  char *body = malloc(body_len + 1);
  if (!body) {
    perror("Failed to allocate memory for response body");
    free(status_line);
    return NULL;
  }
  memcpy(body, body_start, body_len);
  body[body_len] = '\0';

  // look for content type header
  const char *content_type = "text/plain"; // just fallback
  char *ct_start = strcasestr(response_buffer, "Content-Type:");
  if (ct_start) {
    ct_start += strlen("Content-Type:");
    while (*ct_start == ' ')
      ct_start++; // skip whitespace
    char *ct_end = strstr(ct_start, "\r\n");
    if (ct_end) {
      size_t ct_len = ct_end - ct_start;
      char *ct = malloc(ct_len + 1);
      strncpy(ct, ct_start, ct_len);
      ct[ct_len] = '\0';
      content_type = ct;
    }
  }

  // finally build response struct to return to handle_request()
  HttpResponse *response = malloc(sizeof(HttpResponse));
  response->status = status_line;
  response->body = body;
  response->body_length = body_len;
  response->content_type = strdup(content_type);
  response->connection = strdup("close");
  response->date = strdup("Thu, 01 Jan 1970 00:00:00 GMT");
  response->last_modified = strdup("Thu, 01 Jan 1970 00:00:00 GMT");
  response->server = strdup("http-server");
  response->headers = NULL;
  response->num_headers = 0;

  return response;
}
