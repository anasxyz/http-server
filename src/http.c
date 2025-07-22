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
#include "../include/http.h"
#include "../include/proxy.h"
#include "../include/utils_file.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

// set headers
Header headers[] = {{"Date", ""},          {"Server", ""},
                    {"Last-Modified", ""}, {"Content-Length", ""},
                    {"Content-Type", ""},  {"Connection", ""}};

HttpResponse *create_response(int status_code) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response)
    return NULL;

  // set status line
  response->status_line.http_version = strdup("HTTP/1.1");
  response->status_line.status_code = status_code;
  response->status_line.status_reason =
      strdup(get_status_reason(response->status_line.status_code));

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

    size_t error_size = 0;
    response->body = get_body_from_file(error_path, &error_size);
    response->body_length = error_size;

    free(error_path);
  } else {
    response->body = NULL;
    response->body_length = 0;
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
char *serialise_response(HttpResponse *response) {
  int size = 1024 + (response->header_count * 128);
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
  return buffer;
}

HttpResponse *handle_get(HttpRequest *request, void *context) {
  char *raw_path = NULL;
  char *resolved_path = NULL;
  HttpResponse *response = NULL;

  // --- static file handling ---
  // check for alias matches
  raw_path = check_for_alias_match(request->request_line.path);
  if (!raw_path) {
    // no alias match found
    return create_response(500);
  }

  // try paths from try_files in config
  resolved_path = try_paths(raw_path);
  free(raw_path);
  if (!resolved_path) {
    return create_response(404);
  }

  // --- fill response ---
  size_t body_length = 0;
  char *body = get_body_from_file(resolved_path, &body_length);
  if (!body) {
    free(resolved_path);
    return create_response(404);
  }

  response = create_response(200);
  response->body = body;
  response->body_length = body_length;

  // set content type
  const char *mime_type = get_mime_type(resolved_path);
  set_header(response, "Content-Type", mime_type);

  // set content length
  char len_buffer[32];
  snprintf(len_buffer, sizeof(len_buffer), "%zu", body_length);
  set_header(response, "Content-Length", len_buffer);

  // set connection
  set_header(response, "Connection", "close");

  free(resolved_path);

  return response;
}


void handle_request(int socket, char *request_buffer) {
  HttpRequest *request = NULL;
  HttpResponse *response = NULL;
  char *http_str = NULL;

  request = parse_request(request_buffer);
  if (!request) {
    response = create_response(400); // bad request
    goto send;
  }

  // --- proxy handling ---
  // if proxy rule matches, delegate to proxy rule
  // this happens before method specific handling
  Proxy *proxy = find_proxy_for_path(request->request_line.path);
  if (proxy) {
    response = proxy_request(proxy, request_buffer);
    if (!response) {
      response = create_response(502); // bad gateway
    }
    goto send;
  }

  // --- method specific handling ---
  // if no proxy rule matches, handle methods internally
  MethodHandler handlers[] = {
      {"GET", handle_get, NULL},
      {"POST", NULL, NULL},
  };

  int num_handlers = sizeof(handlers) / sizeof(handlers[0]);
  int handler_found = 0;
  for (int i = 0; i < num_handlers; i++) {
    if (strcmp(request->request_line.method, handlers[i].method) == 0) {
      response = handlers[i].handler(request, handlers[i].context);
      handler_found = 1;
      break;
    }
  }

  if (!handler_found) {
    // if no handler found for method, and it wasn't proxied
    response = create_response(405); // method not allowed
    goto send;
  }

  // if handlder returns NULL it means it failed to handle the request
  // the handler itself should have created an appropriate response
  // if it returns NULL here then it implies an unhandled internal error
  if (!response) {
    response = create_response(500); // internal server error
    goto send;
  }

send:
  if (response) {
    http_str = serialise_response(response);
    if (http_str) {
      send(socket, http_str, strlen(http_str), 0);

      // send binary body separately
      if (response->body && response->body_length > 0) {
        send(socket, response->body, response->body_length, 0);
      }
    } else {
      // failed to serialise response, send raw 500 response
      const char* internal_error_response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
      send(socket, internal_error_response, strlen(internal_error_response), 0);
    }
  }

  // free resources
  if (http_str)
    free(http_str);
  if (request)
    free_request(request);
  if (response)
    free_response(response);
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
