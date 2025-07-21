#include <arpa/inet.h>
#include <ctype.h>
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

#define DEFAULT_HTTP_PORT 80

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

  int header_count = sizeof(headers) / sizeof(Header);
  response->headers = malloc(sizeof(Header) * header_count);
  response->header_count = header_count;
  for (int i = 0; i < header_count; i++) {
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

char *try_paths(const char *path) {
  if (!path)
    return NULL;

  char *resolved = NULL;

  // 1. Try the exact path directly (file or dir)
  printf("Trying exact path: %s\n", path);
  resolved = realpath(path, NULL);
  if (resolved && !is_dir(path)) {
    char *result = strdup(resolved);
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
        char *result = strdup(resolved);
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
    if (!html_path)
      return NULL;

    strcpy(html_path, path);
    strcat(html_path, ".html");
    printf("Trying .html fallback: %s\n", html_path);

    resolved = realpath(html_path, NULL);
    if (resolved) {
      char *result = strdup(resolved);
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

  for (size_t i = 0; i < ALIASES_COUNT; i++) {
    const char *prefix = ALIASES[i].from;
    const char *mapped_path = ALIASES[i].to;

    printf("Checking alias: \"%s\" -> \"%s\"\n", prefix, mapped_path);

    if (strncmp(request_path, prefix, strlen(prefix)) == 0) {
      const char *suffix = request_path + strlen(prefix);
      printf("Alias match found! Replacing \"%s\" with \"%s\"\n", prefix,
             mapped_path);
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

Proxy *find_proxy_for_path(const char *request_path) {
  for (size_t i = 0; i < PROXIES_COUNT; i++) {
    size_t prefix_len = strlen(PROXIES[i].from);

    // If proxy->from ends with '/', remove it for matching
    bool ends_with_slash =
        (prefix_len > 0 && PROXIES[i].from[prefix_len - 1] == '/');

    if (ends_with_slash) {
      // Match with and without trailing slash
      // Case 1: request_path starts exactly with PROXIES[i].from
      if (strncmp(request_path, PROXIES[i].from, prefix_len) == 0)
        return &PROXIES[i];

      // Case 2: match request_path == proxy prefix without trailing slash
      if (strncmp(request_path, PROXIES[i].from, prefix_len - 1) == 0 &&
          (request_path[prefix_len - 1] == '\0' ||
           request_path[prefix_len - 1] == '/'))
        return &PROXIES[i];
    } else {
      // If proxy->from does not end with '/', just match prefix normally
      if (strncmp(request_path, PROXIES[i].from, prefix_len) == 0)
        return &PROXIES[i];
    }
  }
  return NULL;
}

// Utility: Parse the proxy->to URL into host and port.
// Returns 0 on success, -1 on failure.
int parse_proxy_target(const char *url, char *host, size_t host_size,
                       int *port) {
  if (!url || !host || !port)
    return -1;

  const char *p = url;

  // Skip scheme if present: http:// or https://
  if (strncmp(p, "http://", 7) == 0) {
    p += 7;
  } else if (strncmp(p, "https://", 8) == 0) {
    p += 8;
    // For simplicity, treat https as http (no TLS)
  }

  // Now p points at host[:port][/...]

  // Find end of host (':' or '/' or end)
  size_t i = 0;
  while (p[i] && p[i] != ':' && p[i] != '/')
    i++;

  if (i >= host_size)
    i = host_size - 1;
  strncpy(host, p, i);
  host[i] = '\0';

  *port = DEFAULT_HTTP_PORT; // default

  // If next char is ':', parse port
  if (p[i] == ':') {
    i++; // move past ':'
    // parse port digits until '/' or end
    int j = i;
    while (p[j] && isdigit((unsigned char)p[j]))
      j++;

    char port_str[6] = {0}; // max 65535 + null
    size_t len = j - i;
    if (len >= sizeof(port_str))
      return -1; // port too long

    strncpy(port_str, p + i, len);
    *port = atoi(port_str);
    if (*port <= 0 || *port > 65535)
      *port = DEFAULT_HTTP_PORT;
  }

  return 0;
}

// Helper: returns a new malloc'ed string with the path prefix stripped
char *strip_prefix(const char *path, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  if (strncmp(path, prefix, prefix_len) == 0) {
    // If the prefix matches, return strdup of the suffix
    return strdup(path + prefix_len);
  }
  return strdup(path); // no prefix match, return original path copy
}

HttpResponse *proxy_request(HttpRequest *request, Proxy *proxy,
                            const char *original_request_str) {
  char host[256];
  int port = 0;

  if (parse_proxy_target(proxy->to, host, sizeof(host), &port) != 0) {
    fprintf(stderr, "Failed to parse proxy target: %s\n", proxy->to);
    return NULL;
  }

  printf("Proxy forwarding to host: %s port: %d\n", host, port);

  // Modify the original request line to strip proxy prefix
  // Extract first line (request line) from original_request_str
  const char *first_line_end = strstr(original_request_str, "\r\n");
  if (!first_line_end) {
    fprintf(stderr, "Invalid HTTP request string\n");
    return NULL;
  }

  size_t first_line_len = first_line_end - original_request_str;
  char *first_line = malloc(first_line_len + 1);
  if (!first_line)
    return NULL;
  strncpy(first_line, original_request_str, first_line_len);
  first_line[first_line_len] = '\0';

  // Parse request line: METHOD PATH VERSION
  char method[16], path[1024], version[16];
  if (sscanf(first_line, "%15s %1023s %15s", method, path, version) != 3) {
    fprintf(stderr, "Failed to parse request line\n");
    free(first_line);
    return NULL;
  }
  free(first_line);

  // Strip the proxy->from prefix from path
  char *new_path = strip_prefix(path, proxy->from);
  if (!new_path)
    return NULL;

  if (new_path[0] != '/') {
    // Make sure new path starts with '/'
    char *fixed_path = malloc(strlen(new_path) + 2);
    if (!fixed_path) {
      free(new_path);
      return NULL;
    }
    fixed_path[0] = '/';
    strcpy(fixed_path + 1, new_path);
    free(new_path);
    new_path = fixed_path;
  }

  // Build new request line string
  char new_request_line[1152]; // enough buffer
  snprintf(new_request_line, sizeof(new_request_line), "%s %s %s\r\n", method,
           new_path, version);
  free(new_path);

  // Build the modified request string: new_request_line + rest of
  // original_request_str after first \r\n
  const char *rest = first_line_end + 2; // skip \r\n
  size_t rest_len = strlen(rest);
  size_t new_request_len = strlen(new_request_line) + rest_len + 1;

  char *modified_request_str = malloc(new_request_len);
  if (!modified_request_str)
    return NULL;
  strcpy(modified_request_str, new_request_line);
  strcat(modified_request_str, rest);

  // Now continue as before: connect, send modified_request_str, receive
  // response

  // Resolve host
  struct addrinfo hints = {0}, *res, *p;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", port);

  int err = getaddrinfo(host, port_str, &hints, &res);
  if (err != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    free(modified_request_str);
    return NULL;
  }

  int sockfd = -1;
  for (p = res; p != NULL; p = p->ai_next) {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd == -1)
      continue;

    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0)
      break;

    close(sockfd);
    sockfd = -1;
  }
  freeaddrinfo(res);

  if (sockfd == -1) {
    perror("Could not connect to proxy target");
    free(modified_request_str);
    return NULL;
  }

  size_t len = strlen(modified_request_str);
  ssize_t sent = 0;
  while (sent < (ssize_t)len) {
    ssize_t n = send(sockfd, modified_request_str + sent, len - sent, 0);
    if (n <= 0) {
      perror("send failed");
      close(sockfd);
      free(modified_request_str);
      return NULL;
    }
    sent += n;
  }

  free(modified_request_str);

  // Read response as you already do ...
  // (rest of your code unchanged)

  size_t bufsize = 8192;
  size_t offset = 0;
  char *buffer = malloc(bufsize);
  if (!buffer) {
    close(sockfd);
    return NULL;
  }

  ssize_t nread;
  while ((nread = recv(sockfd, buffer + offset, bufsize - offset, 0)) > 0) {
    offset += nread;
    if (offset + 1024 > bufsize) {
      bufsize *= 2;
      char *tmp = realloc(buffer, bufsize);
      if (!tmp) {
        free(buffer);
        close(sockfd);
        return NULL;
      }
      buffer = tmp;
    }
  }

  if (nread < 0) {
    perror("recv failed");
    free(buffer);
    close(sockfd);
    return NULL;
  }

  close(sockfd);

  buffer[offset] = '\0';

  HttpResponse *response = parse_response(buffer);
  free(buffer);

  if (!response) {
    fprintf(stderr, "Failed to parse proxy response\n");
    return NULL;
  }

  return response;
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

    // Proxy Handling
    Proxy *proxy = find_proxy_for_path(request->request_line.path);
    if (proxy) {
        response = proxy_request(request, proxy, request_buffer);
        if (!response) {
            response = create_response(502);
        }
        goto send;
    }

    // Static File Handling
    raw_path = resolve_request_path(request->request_line.path);
    if (!raw_path) {
        response = create_response(404);
        goto send;
    }

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

cleanup:
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
