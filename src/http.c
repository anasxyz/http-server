#include <arpa/inet.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/config.h"

#include "../include/file_handler.h"
#include "../include/http.h"
#include "../include/proxy.h"
#include "../include/route.h"
#include "../include/utils_general.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

HttpResponse *create_response(int status_code, char *path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response) {
    perror("Failed to allocate memory for response");
    return NULL;
  }

  char *prepared_path = path_pipeline(path);
  char *body = NULL;
  size_t body_length = 0;
  FILE *file = get_file(prepared_path);

  if (!file) {
    free(prepared_path);
    prepared_path = strdup_printf("%s/404.html", WEB_ROOT);
    file = get_file(prepared_path);

    if (!file) {
      // fallback 500
      status_code = 500;
      body = FALLBACK_500;
      body_length = strlen(body);
    } else {
      body = read_file(file, &body_length);
      if (!body) {
        // fallback 500
        status_code = 500;
        body = FALLBACK_500;
        body_length = strlen(body);
      }
    }
  } else {
    body = read_file(file, &body_length);
    if (!body) {
      // fallback 500
      status_code = 500;
      body = FALLBACK_500;
      body_length = strlen(body);
    }
  }

  response->status = strdup_printf("HTTP/1.1 %d %s", status_code,
                                   get_status_reason(status_code));
  response->body = body;
  response->body_length = body_length;
  response->content_type = get_mime_type(prepared_path);
  response->connection = "close";
  response->date = "Thu, 01 Jan 1970 00:00:00 GMT";
  response->last_modified = "Thu, 01 Jan 1970 00:00:00 GMT";
  response->server = "http-server";
  response->headers = NULL;
  response->num_headers = 0;

  free(prepared_path);
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
           response->status, response->date, response->server,
           response->last_modified, response->body_length,
           response->content_type, response->connection);

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
  HttpResponse *response;

  // parse request
  HttpRequest request = parse_request(request_buffer);

  // check if request is valid
  if (!request.method || !request.path || !request.version) {
    response = create_response(400, request.path);
    if (!response) {
      return;
    }
    send_response(socket, response);
    free(response);
    return;
  }

  // choose static or dynamic response based on request
  Route *matched = match_route(clean_path(request.path));

  if (matched) {
    char *trimmed_path = trim_prefix(request.path, matched->prefix);

    // combine matched->backend_path and trimmed_path
    // avoid "//" or missing "/"
    char full_backend_path[1024];
    snprintf(full_backend_path, sizeof(full_backend_path), "%s/%s",
             matched->backend_path, trimmed_path);

    char *normalised_path = clean_path(full_backend_path);

    request.path = normalised_path;

    printf("Matched route: prefix=%s, host=%s, port=%d, backend path=%s, final "
           "path=%s\n",
           matched->prefix, matched->host, matched->port, matched->backend_path,
           request.path);

    response = proxy_to_backend(request, matched->host, matched->port);

    free(trimmed_path);
    free(normalised_path);
  } else {
    response = create_response(200, request.path); // static
  }

  if (!response) {
    return;
  }
  send_response(socket, response);
  free(response);
}
