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
  if (!path) return NULL;

  char* resolved = NULL;

  // 1. Try the exact path directly (file or dir)
  printf("Trying exact path: %s\n", path);
  resolved = realpath(path, NULL);
  if (resolved && !is_dir(path)) {
    char* result = strdup(resolved);
    free(resolved);
    return result;
  }
  free(resolved);

  // 2. If it's a directory, try known fallback files (like index.html, etc.)
  if (is_dir(path)) {
    size_t try_count = TRY_FILES_COUNT;

    for (size_t i = 0; i < try_count; i++) {
      char *candidate = join_paths(path, TRY_FILES[i]);
      printf("Trying fallback: %s\n", candidate);

      resolved = realpath(candidate, NULL);
      if (resolved) {
        char* result = strdup(resolved);
        free(resolved);
        free(candidate);
        return result;
      }
      free(candidate);
      free(resolved);
    }
  }

  // 3. If it's not a directory, try adding .html extension
  if (!is_dir(path)) {
    size_t len = strlen(path);
    char *html_path = malloc(len + 6); // +5 for ".html" +1 for '\0'
    if (!html_path) return NULL;

    strcpy(html_path, path);
    strcat(html_path, ".html");
    printf("Trying .html fallback: %s\n", html_path);

    resolved = realpath(html_path, NULL);
    if (resolved) {
      char* result = strdup(resolved);
      free(resolved);
      free(html_path);
      return result;
    }
    free(html_path);
    free(resolved);
  }

  return NULL;
}

char *resolve_request_path(const char *request_path) {
  printf("Resolving request path: %s\n", request_path);

  for (int i = 0; i < ALIASES_COUNT; i++) {
    const char *prefix = ALIASES[i].from;
    const char *mapped_path = ALIASES[i].to;

    printf("Checking alias: \"%s\" -> \"%s\"\n", prefix, mapped_path);

    if (strncmp(request_path, prefix, strlen(prefix)) == 0) {
      const char *suffix = request_path + strlen(prefix);
      printf("Alias match found! Replacing \"%s\" with \"%s\"\n", prefix, mapped_path);
      printf("Suffix after prefix: \"%s\"\n", suffix);

      // check if mapped_path is a file
      if (!is_dir(mapped_path)) {
        printf("Alias target is a file. Using it directly: %s\n", mapped_path);
        return strdup(mapped_path);
      }

      // if it's a dir, join suffix
      char *final_path = join_paths(mapped_path, suffix);
      printf("Resolved path using alias: %s\n", final_path);
      return final_path;
    }
  }

  // no alias matched sofall back to default root
  printf("No alias matched. Falling back to root: %s\n", ROOT);
  char *fallback_path = join_paths(ROOT, request_path);
  printf("Resolved path using root: %s\n", fallback_path);

  return fallback_path;
}

void handle_request(int socket, char *request_buffer) {
  char* http_str = NULL;

  HttpRequest *request = parse_request(request_buffer);
  if (request) {
    HttpResponse *response = create_response();
    if (response) {
      char* raw_path = resolve_request_path(request->request_line.path);
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
