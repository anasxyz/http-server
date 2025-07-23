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
#include <sys/stat.h>
#include <sys/sendfile.h>

#include "../include/config.h"
#include "../include/http.h"
#include "../include/proxy.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

// set headers
Header headers[] = {{"Date", ""},          {"Server", ""},
                    {"Last-Modified", ""}, {"Content-Length", ""},
                    {"Content-Type", ""},  {"Connection", ""}};

MethodHandler handlers[] = {
    {"GET", handle_get},
};

HttpResponse *create_response(int status_code, char *path) {
  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response)
    return NULL;

  char* content_type = NULL;

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

  // if error
  if (status_code >= 400 && !path) {
    char error_file_path[512];
    snprintf(error_file_path, sizeof(error_file_path), "%s/%d.html", ROOT, status_code);

    // open error file
    response->file_fd = open(error_file_path, O_RDONLY);

    // get error file size
    struct stat st;
    fstat(response->file_fd, &st);
    response->file_size = st.st_size;

    content_type = get_mime_type(error_file_path);
  } else {
    // open file
    response->file_fd = open(path, O_RDONLY);

    // get file size
    struct stat st;
    fstat(response->file_fd, &st);
    response->file_size = st.st_size;
    
    content_type = get_mime_type(path);
  }

  char content_length[32];
  // convert size_t to string
  snprintf(content_length, sizeof(content_length), "%zu", response->file_size);

  char* date = http_date_now();
  char *last_modified = http_date_now();

  set_header(response, "Server", "http-server");
  set_header(response, "Date", date);
  set_header(response, "Connection", "close"); 
  set_header(response, "Content-Type", content_type);
  set_header(response, "Content-Length", content_length);
  set_header(response, "Last-Modified", last_modified);

  free(date);
  free(last_modified);

  return response;
}

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

HttpResponse *handle_get(HttpRequest *request) {
  char *path = NULL;
  char *resolved_path = NULL;
  HttpResponse *response = NULL;

  // --- check for alias matches ---
  path = check_for_alias_match(request->request_line.path);

  // --- try files from config if no aliases matched --- 
  resolved_path = try_paths(path);
  if (!resolved_path) {
    // path doesn't exist - page not found
    response = create_response(404, NULL);
    return response;
  }

  // if path exists
  response = create_response(200, resolved_path);

  free(path);
  free(resolved_path);

  return response;
}

void handle_request(int socket, char *request_buffer) {
  HttpRequest *request = NULL;
  HttpResponse *response = NULL;
  ProxyResult *proxy_result = NULL;
  char *http_str = NULL;

  request = parse_request(request_buffer);
  if (!request) {
    response = create_response(400, NULL); // bad request
    goto send;
  }

  // --- proxy handling ---
  // if proxy rule matches, delegate to proxy rule
  // this happens before method specific handling
  Proxy *proxy = find_proxy_for_path(request->request_line.path);
  if (proxy) {
    proxy_result = proxy_request(proxy, request_buffer);
    if (!proxy_result) {
      response = create_response(503, NULL); // bad gateway
      goto send;
    }
    goto proxy_send;
  }

  // --- method specific handling ---
  // if no proxy rule matches, handle methods internally
  int num_handlers = sizeof(handlers) / sizeof(handlers[0]);
  int handler_found = 0;
  for (int i = 0; i < num_handlers; i++) {
    if (strcmp(request->request_line.method, handlers[i].method) == 0) {
      response = handlers[i].handler(request);
      handler_found = 1;
      break;
    }
  }
  if (!handler_found) {
    // if no handler found for method, and it wasn't proxied
    response = create_response(405, NULL); // method not allowed
    goto send;
  }

send:
  if (response) {
    http_str = serialise_response(response);
    if (http_str) {
      // send headers
      send(socket, http_str, strlen(http_str), 0);

      sendfile(socket, response->file_fd, 0, response->file_size);
    }
  }

proxy_send:
  if (proxy_result) {
    send(socket, proxy_result->headers, strlen(proxy_result->headers), 0);
    send(socket, proxy_result->body, strlen(proxy_result->body), 0);

    free(proxy_result->headers);
    free(proxy_result->body);
    free(proxy_result);
  }

  // free resources
  if (http_str)
    free(http_str);
  if (request)
    free_request(request);
  if (response)
    free_response(response);
}
