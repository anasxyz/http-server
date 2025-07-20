#include <arpa/inet.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/utils_file.h"
#include "../include/utils_path.h"
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
  Header headers[] = {{"Date", ""},          {"Server", ""},
                      {"Last-Modified", ""}, {"Content-Length", ""},
                      {"Content-Type", ""},  {"Connection", ""}};

  int header_count = sizeof(headers) / sizeof(Header);
  response->headers = malloc(sizeof(Header) * header_count);
  response->header_count = header_count;

  for (int i = 0; i < header_count; i++) {
    response->headers[i].key = strdup(headers[i].key);
    response->headers[i].value = strdup(headers[i].value);
  }

  // set body
  response->body = NULL;

  return response;
}

void send_response(int socket, HttpResponse *response) {
  // not yet
}

HttpResponse *handle_get(HttpRequest *request, void *context) {
  // not yet
  return NULL;
}

HttpResponse *handle_post(HttpRequest *request, void *context) {
  // not yet
  return NULL;
}

// serialise response to string.
// include_body: whether to include the body in the response.
char *serialise_response(HttpResponse *response, bool include_body) {
  // estimate maximum size
  int size = 1024 + (response->header_count * 128);
  if (include_body && response->body) {
    size += strlen(response->body);
  }

  char *buffer = malloc(size);
  if (!buffer)
    return NULL;

  int offset = snprintf(
      buffer, size, "%s %d %s\r\n", response->status_line.http_version,
      response->status_line.status_code, response->status_line.status_reason);

  for (int i = 0; i < response->header_count; i++) {
    offset += snprintf(buffer + offset, size - offset, "%s: %s\r\n",
                       response->headers[i].key, response->headers[i].value);
  }

  offset += snprintf(buffer + offset, size - offset, "\r\n");

  if (include_body && response->body) {
    snprintf(buffer + offset, size - offset, "%s", response->body);
  }

  return buffer;
}

char* try_paths(const char* path) {
  char* resolved = NULL;
  char* file_path = NULL;

  if (!path) return NULL;

  // 1. Direct file (not dir)
  resolved = realpath(path, NULL);
  if (resolved && !is_dir(path)) {
    char* result = strdup(resolved);
    free(resolved);
    return result;
  }
  free(resolved);

  // 2. path/index.html if it's a dir
  resolved = realpath(path, NULL);
  if (resolved && is_dir(path)) {
    file_path = join_paths(path, "index.html");
    free(resolved);

    resolved = realpath(file_path, NULL);
    if (resolved) {
      char* result = strdup(resolved);
      free(resolved);
      free(file_path);
      return result;
    }
    free(resolved);
    free(file_path);
    file_path = NULL;
  } else {
    free(resolved);
  }

  // 3. path.html
  if (!is_dir(path)) {
    size_t len = strlen(path);
    file_path = malloc(len + 6); // ".html" + '\0'
    if (!file_path) return NULL;

    strcpy(file_path, path);
    strcat(file_path, ".html");

    resolved = realpath(file_path, NULL);
    if (resolved) {
      char* result = strdup(resolved);
      free(resolved);
      free(file_path);
      return result;
    }
    free(resolved);
    free(file_path);
  }

  return NULL;
}

void handle_request(int socket, char *request_buffer) {
  char* http_str = NULL;

  HttpRequest *request = parse_request(request_buffer);
  if (request) {
    HttpResponse *response = create_response();
    if (response) {
      char* raw_path = join_paths("/var/www/", request->request_line.path);
      char* path = NULL;
      if (raw_path) {
        path = try_paths(raw_path);
        free(raw_path);
        if (!path) {
          // TODO: handle NULL path because path doesn't exist even after trying
          path = strdup("/var/www/404.html"); 
        }

        // get body from file here
        response->body = get_body_from_file(path);
        if (!response->body) {
          // TODO: handle NULL body error
          response->body = strdup("<html><body><h1>404 Not Found</h1></body></html>");
        }

        http_str = serialise_response(response, true);

        // printf("\n====== RESPONSE SENT ======\n");
        // printf("%s\n", http_str);
        // printf("===========================");

        send(socket, http_str, strlen(http_str), 0);
        
        free(http_str);
        free(path);
        free_request(request);
        free_response(response);
      } else {
        // TODO: handle NULL path error
        free_request(request);
        free_response(response);
      }
    } else {
      // TODO: handle NULL response error
      free_request(request);
    }
  } else {
    // TODO: handle NULL request error
  }
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
