#include <arpa/inet.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/http.h"
#include "../include/utils_http.h"

HttpResponse *create_response() {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response)
    return NULL;

  // set status line
  response->status_line.http_version = strdup("HTTP/1.1");
  response->status_line.status_code = 200;
  response->status_line.status_reason =
      strdup(get_status_reason(response->status_line.status_code));

  // set headers
  Header headers[] = {
    {"Date", ""},          
    {"Server", ""},
    {"Last-Modified", ""}, 
    {"Content-Length", ""},
    {"Content-Type", ""},  
    {"Connection", ""}
  };

  int header_count = sizeof(headers) / sizeof(Header);
  response->headers = malloc(sizeof(Header) * header_count);
  response->header_count = header_count;

  for (int i = 0; i < header_count; i++) {
    response->headers[i].key = strdup(headers[i].key);
    response->headers[i].value = strdup(headers[i].value);
  }

  // set body
  response->body = "<html><body><h1>Hello, World!</h1></body></html>";

  return response;
}



void send_response(int socket, HttpResponse *response) {
  // not yet
}

HttpResponse *handle_get(HttpRequest *request, void *context) {
  // not yet
}

HttpResponse *handle_post(HttpRequest *req, void *ctx) {
  // not yet
}

char *serialise_response(HttpResponse *response) {
  // estimate maximum size
  int size =
      1024 + (response->header_count * 128) + (response->body ? strlen(response->body) : 0);
  char *buffer = malloc(size);
  if (!buffer)
    return NULL;

  int offset =
      snprintf(buffer, size, "%s %d %s\r\n", response->status_line.http_version,
               response->status_line.status_code, response->status_line.status_reason);

  for (int i = 0; i < response->header_count; i++) {
    offset += snprintf(buffer + offset, size - offset, "%s: %s\r\n",
                       response->headers[i].key, response->headers[i].value);
  }

  offset += snprintf(buffer + offset, size - offset, "\r\n");

  if (response->body) {
    snprintf(buffer + offset, size - offset, "%s", response->body);
  }

  return buffer;
}

void handle_request(int socket, char *request_buffer) {
  HttpResponse *response = create_response();
  
  // get body from file here

  char *http_str = serialise_response(response);
  printf("====== RESPONSE SENT ======\n");
  printf("%s\n", http_str);
  printf("===========================");
  send(socket, http_str, strlen(http_str), 0);
}

/*
void handle_request(int socket, char *request_buffer) {
  HttpRequest request = parse_request(request_buffer);
  if (!request.method || !request.path || !request.version) {
    HttpResponse *response = create_response(400, request.path);
    if (response) {
      send_response(socket, response);
      free_response(response);
    }
    return;
  }

  char *cleaned_path = clean_path(request.path);
  Route *matched = match_route(cleaned_path);
  free(cleaned_path);

  // define supported methods and their handlers
  MethodHandler handlers[] = {
      {"GET", handle_get, matched},
      {"POST", handle_post, matched},
  };

  HttpResponse *response = NULL;
  size_t num_handlers = sizeof(handlers) / sizeof(handlers[0]);

  for (size_t i = 0; i < num_handlers; i++) {
    if (strcmp(request.method, handlers[i].method) == 0) {
      response = handlers[i].handler(&request, handlers[i].context);
      break;
    }
  }

  // if no handler found for method
  if (!response) {
    response = create_response(405, NULL); // Method Not Allowed
    send_response(socket, response);
    free_response(response);
  } else {
    send_response(socket, response);
    free_response(response);
  }
}
*/
