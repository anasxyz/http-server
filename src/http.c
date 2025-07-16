#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/file_handler.h"
#include "../include/http.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

HttpResponse* create_response(int status_code, const char* path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate memory for response...\n");
    return NULL;
  }

  if (status_code >= 400 && status_code < 500) {
    char error_path[32];
    snprintf(error_path, sizeof(error_path), "/%d.html", status_code);
    path = resolve_path(error_path);
  }

  const char *reason = get_status_reason(status_code);
  int status_len = snprintf(NULL, 0, "HTTP/1.1 %d %s", status_code, reason);
  response->status = malloc(status_len + 1);
  snprintf(response->status, status_len + 1, "HTTP/1.1 %d %s", status_code, reason);

  char* content_type = strdup(get_mime_type(path));  // strdup ensures ownership

  FILE *file = get_file(path);
  if (!file) {
    fprintf(stderr, "Failed to open file: %s\n", path);
    free(response->status);
    free(response->content_type);
    free(response);
    return NULL;
  }

  char *body = read_file(file);
  fclose(file);

  if (!body) {
    fprintf(stderr, "Failed to read file into memory: %s\n", path);
    free(response->status);
    free(response->content_type);
    free(response);
    return NULL;
  }

  response->body = body;
  response->body_length = strlen(body);  // only safe because read_file null-terminates
  response->content_type = content_type;
  response->connection = "close";
  response->date = "Thu, 01 Jan 1970 00:00:00 GMT";
  response->last_modified = "Thu, 01 Jan 1970 00:00:00 GMT";
  response->server = "http-server";
  response->headers = NULL;
  response->num_headers = 0;

  return response;
}

// sends HTTP response
void send_response(int socket, HttpResponse *response) {
  char header[512];
  snprintf(header, sizeof(header),
           "%s\r\n"
           "Date: %s\r\n"
           "Server: %s\r\n"
           "Last-Modified: %s\r\n"
           "Content-Length: %lu\r\n"
           "Content-Type: %s\r\n"
           "Connection: %s\r\n"
           "\r\n",
           response->status, 
           response->date, 
           response->server, 
           response->last_modified, 
           response->body_length,
           response->content_type,
           response->connection);

  // TODO: explore possibility of extra headers in the future

  write(socket, header, strlen(header));
  write(socket, response->body, response->body_length);

  printf("Sent response: \n");
  printf("Status: %s\n", response->status);
  printf("Date: %s\n", response->date);
  printf("Server: %s\n", response->server);
  printf("Last-Modified: %s\n", response->last_modified);
  printf("Content-Type: %s\n", response->content_type);
  printf("Content-Length: %lu\n", response->body_length);
  printf("Connection: %s\n", response->connection);
}

// handles HTTP request
void handle_request(int socket, char *request_buffer) {
  HttpResponse *response = NULL;
  HttpRequest request = parse_request(request_buffer);

  // if request is bad, send 400 Bad Request
  if (!request.method || !request.path || !request.version) {
    create_response(400, NULL);
  }

  const char *final_path = resolve_path(request.path);

  // only support GET requests for now
  if (!is_method_allowed(request.method)) {
    response = create_response(405, NULL);
  } else if (does_path_exist(final_path)) {
    response = create_response(200, final_path);
  } else {
    response = create_response(404, final_path);
  }

  send_response(socket, response);
  free(response); 
}
