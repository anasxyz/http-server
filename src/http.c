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

#define FALLBACK_500 "<html><head><title>500 Internal Server Error</title></head><body><h1>500 Internal Server Error</h1></body></html>"

char* prepare_path(char *path) {
  char *cleaned_path = clean_path(path);
  char *full_path = get_full_path(cleaned_path);
  char *resolved_path = resolve_path(full_path);

  return resolved_path;
}

HttpResponse* create_response(int status_code, char* path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate memory for response...\n");
    return NULL;
  }

  // just for clarity 
  char* provided_path = path;
  char* cleaned_path = clean_path(provided_path);
  char* full_path = get_full_path(cleaned_path);
  char* resolved_path = resolve_path(full_path);

  char* body;

  FILE* file = get_file(resolved_path);
  // if file not found or couldn't open
  if (!file) { 
    // if file not found or couldn't open
    status_code = 404;
    // set new path to 404 page
    resolved_path = "www/404.html";
    // try to open 404 page
    file = get_file(resolved_path);
    if (!file) { 
      // if we still can't open 404 page, we can assume it's a 500
      status_code = 500; 
      body = FALLBACK_500;
    } else {
      // 404 page opened successfully, read it
      body = read_file(file);  
      // if we can't read the 404 page, assume 500
      if (!body) { 
        status_code = 500; 
        body = FALLBACK_500;
      }
    }
  } else {
    // if we get here, we know the file exists
    body = read_file(file);
    // if we can't read the file then we can assume it's a 500 because the file exists but
    // we can't read it
    if (!body) { 
      status_code = 500; 
      body = FALLBACK_500;
    }
  }

  /*
  body = strdup_printf(
            "Provided path: %s\n"
            "Cleaned path: %s\n"
            "Full path: %s\n"
            "Resolved path: %s\n",
            provided_path,
            cleaned_path,
            full_path,
            resolved_path);
  */

  // mock response
  response->status = strdup_printf("HTTP/1.1 %d %s", status_code, get_status_reason(status_code));
  response->body = body;
  response->body_length = strlen(body);
  response->content_type = "text/html";
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
  if (!response) { return; }
  send_response(socket, response);
  free(response);
}
