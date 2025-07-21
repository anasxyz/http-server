#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/proxy.h"
#include "../include/http.h"
#include "../include/utils_file.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

HttpResponse *create_response(int status_code) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response)
    return NULL;

  // set status line
  response->status_line.http_version = strdup("HTTP/1.1");
  response->status_line.status_code = status_code;
  response->status_line.status_reason =
      strdup(get_status_reason(response->status_line.status_code));

  // set headers
  Header headers[] = {{"Date", ""},          {"Server", ""},
                      {"Last-Modified", ""}, {"Content-Length", ""},
                      {"Content-Type", ""},  {"Connection", ""}};

  size_t header_count = sizeof(headers) / sizeof(Header);
  response->headers = malloc(sizeof(Header) * header_count);
  response->header_count = header_count;
  for (size_t i = 0; i < header_count; i++) {
    response->headers[i].key = strdup(headers[i].key);
    response->headers[i].value = strdup(headers[i].value);
  }

  if (status_code >= 400) {
    char error_file[256];
    snprintf(error_file, sizeof(error_file), "/%d.html", status_code);
    char *error_path = join_paths(ROOT, error_file);
    response->body = get_body_from_file(error_path);
    free(error_path);
  } else {
    response->body = NULL;
  }

  return response;
}

/*
HttpResponse *handle_get(HttpRequest *request, void *context) {
  // not yet
  return NULL;
}

HttpResponse *handle_post(HttpRequest *request, void *context) {
  // not yet
  return NULL;
}
*/

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

  for (size_t i = 0; i < response->header_count; i++) {
    offset += snprintf(buffer + offset, size - offset, "%s: %s\r\n",
                       response->headers[i].key, response->headers[i].value);
  }

  offset += snprintf(buffer + offset, size - offset, "\r\n");

  if (include_body && response->body) {
    snprintf(buffer + offset, size - offset, "%s", response->body);
  }

  return buffer;
}

void handle_request(int socket, char *request_buffer) {
    HttpRequest *request = NULL;
    HttpResponse *response = NULL;
    char *http_str = NULL;
    char *raw_path = NULL;
    char *resolved_path = NULL;

    request = parse_request(request_buffer);
    if (!request) {
        response = create_response(400);
        goto send;
    }

    // --- proxy handling ---
    Proxy *proxy = find_proxy_for_path(request->request_line.path);
    if (proxy) {
        response = proxy_request(proxy, request_buffer);
        if (!response) {
            response = create_response(502);
        }
        goto send;
    }

    // --- static file handling ---
    // check for alias matches
    raw_path = check_for_alias_match(request->request_line.path);
    if (!raw_path) {
        response = create_response(404);
        goto send;
    }

    // try paths from try_files in config
    resolved_path = try_paths(raw_path);
    free(raw_path);

    if (!resolved_path) {
        response = create_response(404);
        goto send;
    }

    char *body = get_body_from_file(resolved_path);
    if (!body) {
        response = create_response(404);
        goto send;
    }

    response = create_response(200);
    response->body = body;

send:
    if (response) {
        http_str = serialise_response(response, true);
        if (http_str) {
            send(socket, http_str, strlen(http_str), 0);
        }
    }

    if (http_str) free(http_str);
    if (resolved_path) free(resolved_path);
    if (request) free_request(request);
    if (response) free_response(response);
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
