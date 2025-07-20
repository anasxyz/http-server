#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/proxy.h"
#include "../include/utils_http.h"

/*
HttpResponse *proxy_to_backend(HttpRequest request, char *host, int port) {
  int backend_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (backend_socket < 0) {
    perror("Failed to create backend socket");
    return NULL;
  }

  struct sockaddr_in backend_addr;
  memset(&backend_addr, 0, sizeof(backend_addr));
  backend_addr.sin_family = AF_INET;
  backend_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &backend_addr.sin_addr) <= 0) {
    perror("Invalid backend address");
    close(backend_socket);
    return NULL;
  }

  if (connect(backend_socket, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0) {
    perror("Failed to connect to backend");
    close(backend_socket);
    return NULL;
  }

  char request_buffer[2048];
  snprintf(request_buffer, sizeof(request_buffer),
           "%s %s HTTP/1.1\r\n"
           "Host: %s:%d\r\n"
           "Connection: close\r\n"
           "\r\n",
           request.method, request.path, host, port);

  if (write(backend_socket, request_buffer, strlen(request_buffer)) < 0) {
    perror("Failed to write to backend");
    close(backend_socket);
    return NULL;
  }

  // read backend response in a loop
  char response_buffer[8192];
  size_t total_received = 0;
  ssize_t bytes_received;

  while ((bytes_received = read(backend_socket, response_buffer + total_received,
                                sizeof(response_buffer) - total_received - 1)) > 0) {
    total_received += bytes_received;
    if (total_received >= sizeof(response_buffer) - 1) break;
  }

  if (bytes_received < 0) {
    perror("Failed to read from backend");
    close(backend_socket);
    return NULL;
  }

  response_buffer[total_received] = '\0';
  close(backend_socket);

  // parse status line
  char *status_line_end = strstr(response_buffer, "\r\n");
  if (!status_line_end) {
    fprintf(stderr, "Malformed response from backend\n");
    return NULL;
  }

  size_t status_line_len = status_line_end - response_buffer;
  char *status_line = strndup(response_buffer, status_line_len);
  if (!status_line) {
    perror("Failed to allocate status_line");
    return NULL;
  }

  char *body_start = strstr(response_buffer, "\r\n\r\n");
  if (!body_start) {
    fprintf(stderr, "No body in backend response\n");
    free(status_line);
    return NULL;
  }
  body_start += 4;

  size_t body_len = total_received - (body_start - response_buffer);
  char *body = malloc(body_len + 1);
  if (!body) {
    perror("Failed to allocate memory for body");
    free(status_line);
    return NULL;
  }
  memcpy(body, body_start, body_len);
  body[body_len] = '\0';

  char *content_type = strdup("text/plain"); // default
  if (!content_type) {
    perror("Failed to allocate content_type");
    free(status_line);
    free(body);
    return NULL;
  }

  char *ct_start = strcasestr(response_buffer, "Content-Type:");
  if (ct_start) {
    ct_start += strlen("Content-Type:");
    while (*ct_start == ' ') ct_start++;
    char *ct_end = strstr(ct_start, "\r\n");
    if (ct_end) {
      size_t ct_len = ct_end - ct_start;
      char *new_ct = malloc(ct_len + 1);
      if (new_ct) {
        strncpy(new_ct, ct_start, ct_len);
        new_ct[ct_len] = '\0';
        free(content_type);
        content_type = new_ct;
      }
    }
  }

  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate HttpResponse");
    free(status_line);
    free(body);
    free(content_type);
    return NULL;
  }

  response->status = status_line;
  response->body = body;
  response->body_length = body_len;
  response->content_type = content_type;
  response->connection = strdup("close");
  response->date = strdup("Thu, 01 Jan 1970 00:00:00 GMT");
  response->last_modified = strdup("Thu, 01 Jan 1970 00:00:00 GMT");
  response->server = strdup("http-server");
  response->headers = NULL;
  response->num_headers = 0;

  if (!response->connection || !response->date || !response->last_modified || !response->server) {
    perror("Failed to allocate headers");
    free(response->status);
    free(response->body);
    free(response->content_type);
    free(response->connection);
    free(response->date);
    free(response->last_modified);
    free(response->server);
    free(response);
    return NULL;
  }

  return response;
}
*/
