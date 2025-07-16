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
#include "../include/utils_general.h"

HttpResponse* create_response(int status_code, char* path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate memory for response...\n");
    return NULL;
  }

  char* provided_path = path;
  char* cleaned_path = clean_path(provided_path);
  char* full_path = get_full_path(cleaned_path);
  char* resolved_path = resolve_path(full_path);

  char *body = strdup_printf(
            "Provided path: %s\n"
            "Cleaned path: %s\n"
            "Full path: %s\n"
            "Resolved path: %s\n",
            provided_path,
            cleaned_path,
            full_path,
            resolved_path);

  // mock response
  response->status = "HTTP/1.1 200 OK";
  response->body = body;
  response->body_length = strlen(response->body);
  response->content_type = "text/plain";
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
  printf("\n");
}

// handles HTTP request
void handle_request(int socket, char *request_buffer) {
  HttpResponse* response;

  // parse request
  HttpRequest request = parse_request(request_buffer);
  if (!request.method || !request.path || !request.version) {
    
  }

  response = create_response(200, request.path);
  send_response(socket, response);
  free(response);
}
