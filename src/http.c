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

  // get full status line
  char status[100];
  snprintf(status, sizeof(status), "HTTP/1.1 %i %s", status_code, get_status_reason(status_code));

  // get content type
  char* content_type = get_mime_type(path);

  // read file into buffer
  FILE *file = get_file(path);
  if (!file) {
    perror("Failed to open file...\n");
    return NULL;
  }
  const char *file_buffer = "<html><body><h1>Test</h1></body></html>";

  // free file
  fclose(file);

  // build response
  response->status = status;
  // response->date = "Thu, 01 Jan 1970 00:00:00 GMT"; // hardcoded for now
  // response->server = "http-server";
  // response->last_modified = "Thu, 01 Jan 1970 00:00:00 GMT"; // hardcoded for now
  response->body_length = strlen(file_buffer);
  response->content_type = "text/html";
  response->connection = "close";

  response->body = file_buffer;
  response->headers = NULL;
  response->num_headers = 0;

  return response;
}

// sends HTTP response
void send_response(int socket, HttpResponse *response) {
  char header[512];
  snprintf(header, sizeof(header),
           "%s\r\n"
           // "Date: %s\r\n"
           // "Server: %s\r\n"
           // "Last-Modified: %s\r\n"
           "Content-Length: %lu\r\n"
           "Content-Type: %s\r\n"
           "Connection: %s\r\n"
           "%s"
           "\r\n",
           response->status, 
           // response->date, 
           // response->server, 
           // response->last_modified, 
           response->body_length,
           response->content_type,
           response->connection,
           response->body);

  // TODO: explore possibility of extra headers in the future

  write(socket, header, strlen(header));
  write(socket, response->body, response->body_length);

  printf("Sent response: \n");
  printf("Status: %s\n", response->status);
  // printf("Date: %s\n", response->date);
  // printf("Server: %s\n", response->server);
  // printf("Last-Modified: %s\n", response->last_modified);
  printf("Content-Type: %s\n", response->content_type);
  printf("Content-Length: %lu\n", response->body_length);
  printf("Connection: %s\n", response->connection);

  free(response);
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

  const char* final_path = get_final_path(request.path);

  if (does_path_exist(final_path) == false) {
    perror("file does not exist...\n");
    return;
  }

  HttpResponse *response = create_response(200, final_path);
  if (!response) {
    return;
  }

  send_response(socket, response);
}
