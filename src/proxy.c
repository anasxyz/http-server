#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/proxy.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

#define DEFAULT_HTTP_PORT 80

// parse the proxy->to url into host and port.
// returns 0 on success or -1 on failure.
int parse_proxy_target(const char *url, char *host, size_t host_size,
                       int *port) {
  if (!url || !host || !port)
    return -1;

  const char *p = url;

  // skip http:// or https:// scheme if present
  if (strncmp(p, "http://", 7) == 0) {
    p += 7;
  } else if (strncmp(p, "https://", 8) == 0) {
    p += 8;
    // treat https as http (no TLS) for now
  }

  // now p points at host[:port][/...]

  // find end of host (':' or '/' or just end)
  size_t i = 0;
  while (p[i] && p[i] != ':' && p[i] != '/')
    i++;

  if (i >= host_size)
    i = host_size - 1;
  strncpy(host, p, i);
  host[i] = '\0';

  *port = DEFAULT_HTTP_PORT; // default

  // if next char is ':' then parse port
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

Proxy *find_proxy_for_path(const char *request_path) {
  for (size_t i = 0; i < PROXIES_COUNT; i++) {
    size_t prefix_len = strlen(PROXIES[i].from);

    // if proxy->from ends with '/' then remove it for matching
    bool ends_with_slash =
        (prefix_len > 0 && PROXIES[i].from[prefix_len - 1] == '/');

    if (ends_with_slash) {
      // match with and without trailing slash
      // Case 1: request_path starts exactly with PROXIES[i].from
      if (strncmp(request_path, PROXIES[i].from, prefix_len) == 0)
        return &PROXIES[i];

      // Case 2: match request_path == proxy prefix without trailing slash
      if (strncmp(request_path, PROXIES[i].from, prefix_len - 1) == 0 &&
          (request_path[prefix_len - 1] == '\0' ||
           request_path[prefix_len - 1] == '/'))
        return &PROXIES[i];
    } else {
      // if proxy->from does not end with '/' just match prefix normally
      if (strncmp(request_path, PROXIES[i].from, prefix_len) == 0)
        return &PROXIES[i];
    }
  }
  return NULL;
}

// returns a new mallocd string with the path prefix stripped
char *strip_prefix(const char *path, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  if (strncmp(path, prefix, prefix_len) == 0) {
    // if the prefix matches, return strdup of the suffix
    return strdup(path + prefix_len);
  }
  return strdup(path); // no prefix match so just return original path copy
}

HttpResponse *proxy_request(Proxy *proxy, const char *original_request_str) {
  char host[256];
  int port = 0;

  if (parse_proxy_target(proxy->to, host, sizeof(host), &port) != 0) {
    fprintf(stderr, "Failed to parse proxy target: %s\n", proxy->to);
    return NULL;
  }

  printf("Proxy forwarding to host: %s port: %d\n", host, port);

  // 1. Extract original request line
  const char *first_line_end = strstr(original_request_str, "\r\n");
  if (!first_line_end) return NULL;

  size_t first_line_len = first_line_end - original_request_str;
  char *first_line = strndup(original_request_str, first_line_len);
  if (!first_line) return NULL;

  char method[16], path[1024], version[16];
  if (sscanf(first_line, "%15s %1023s %15s", method, path, version) != 3) {
    free(first_line);
    return NULL;
  }
  free(first_line);

  // 2. Strip proxy->from prefix
  char *suffix = strip_prefix(path, proxy->from);
  if (!suffix) return NULL;

  // 3. Get base path from proxy->to
  const char *base_path = "/";
  const char *scheme_pos = strstr(proxy->to, "://");
  if (scheme_pos) {
    base_path = strchr(scheme_pos + 3, '/');
    if (!base_path) base_path = "/";
  }

  // 4. Join base path + suffix
  char *full_path = join_paths(base_path, suffix);
  free(suffix);

  // Ensure full_path starts with '/'
  if (full_path[0] != '/') {
    char *fixed = malloc(strlen(full_path) + 2);
    if (!fixed) {
      free(full_path);
      return NULL;
    }
    fixed[0] = '/';
    strcpy(fixed + 1, full_path);
    free(full_path);
    full_path = fixed;
  }

  // 5. Build new request line
  char new_request_line[1152];
  snprintf(new_request_line, sizeof(new_request_line), "%s %s %s\r\n", method, full_path, version);
  free(full_path);

  // 6. Reconstruct modified request string
  const char *rest = first_line_end + 2;
  size_t new_len = strlen(new_request_line) + strlen(rest) + 1;
  char *modified_request_str = malloc(new_len);
  if (!modified_request_str) return NULL;

  strcpy(modified_request_str, new_request_line);
  strcat(modified_request_str, rest);

  // 7. Connect to backend
  struct addrinfo hints = {0}, *res, *p;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", port);

  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    free(modified_request_str);
    return NULL;
  }

  int sockfd = -1;
  for (p = res; p; p = p->ai_next) {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd == -1) continue;
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
    close(sockfd);
    sockfd = -1;
  }
  freeaddrinfo(res);
  if (sockfd == -1) {
    free(modified_request_str);
    return NULL;
  }

  // 8. Send request
  size_t len = strlen(modified_request_str);
  ssize_t sent = 0;
  while (sent < (ssize_t)len) {
    ssize_t n = send(sockfd, modified_request_str + sent, len - sent, 0);
    if (n <= 0) {
      close(sockfd);
      free(modified_request_str);
      return NULL;
    }
    sent += n;
  }
  free(modified_request_str);

  // 9. Read response
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
