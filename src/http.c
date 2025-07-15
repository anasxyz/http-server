#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/file_handler.h"
#include "../include/http.h"
#include "../include/utils_path.h"

char* get_status_reason(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 426: return "Upgrade Required";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown Status";
    }
}

HttpResponse* create_response(int status_code, const char* path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate memory for response...\n");
    return NULL;
  }

  if (status_code >= 400 && status_code < 500) {
    char error_path[32];
    snprintf(error_path, sizeof(error_path), "/%d.html", status_code);
    path = get_final_path(error_path);
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

bool is_method_allowed(const char* method) {
  for (size_t i = 0; i < NUM_ALLOWED_METHODS; ++i) {
    if (strcmp(method, allowed_methods[i].method) == 0 && allowed_methods[i].allowed) {
      return allowed_methods[i].allowed;
    }
  }
  return false;
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
