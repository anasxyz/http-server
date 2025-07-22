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
  (void)context;
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


