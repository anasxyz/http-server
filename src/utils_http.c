#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../include/config.h"
#include "../include/utils_file.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

#define MAX_HEADERS 100
#define MAX_PATH 1024

char *try_paths(const char *request_path) {
  if (!request_path)
    return NULL;

  char *resolved = NULL;

  for (size_t i = 0; i < TRY_FILES_COUNT; i++) {
    const char *rule = TRY_FILES[i];

    // --- Case 1: $uri ---
    if (strcmp(rule, "$uri") == 0) {
      printf("Trying $uri: %s\n", request_path);
      resolved = realpath(request_path, NULL);
      if (resolved && !is_dir(request_path)) {
        char *result = strdup(resolved);
        free(resolved);
        return result;
      }
      free(resolved);
    }

    // --- Case 2: $uri/ ---
    else if (strcmp(rule, "$uri/") == 0) {
      if (is_dir(request_path)) {
        for (size_t j = 0; j < INDEX_FILES_COUNT; j++) {
          char *candidate = join_paths(request_path, INDEX_FILES[j]);
          printf("Trying $uri/ index: %s\n", candidate);
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
    }

    // --- Case 3: Fallback file (e.g., /fallback.html) ---
    else {
      char *fallback_path = NULL;

      // If fallback rule starts with '/', join it with ROOT (strip leading '/')
      if (rule[0] == '/') {
        // remove leading '/'
        fallback_path = join_paths(ROOT, rule + 1);
      } else {
        fallback_path = join_paths(ROOT, rule);
      }

      printf("Trying fallback file with root: %s\n", fallback_path);
      resolved = realpath(fallback_path, NULL);
      free(fallback_path);

      if (resolved) {
        char *result = strdup(resolved);
        free(resolved);
        return result;
      }
      free(resolved);
    }
  }

  return NULL;
}

char *check_for_alias_match(const char *request_path) {
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

      // check if mapped_path is file
      if (!is_dir(mapped_path)) {
        printf("Alias target is a file. Using it directly: %s\n", mapped_path);
        return strdup(mapped_path);
      }

      // if directory join suffix
      char *final_path = join_paths(mapped_path, suffix);
      printf("Resolved path using alias: %s\n", final_path);
      return final_path;
    }
  }

  // no alias matched so fall back to default root
  printf("No alias matched. Falling back to root: %s\n", ROOT);
  char *fallback_path = join_paths(ROOT, request_path);
  printf("Resolved path using root: %s\n", fallback_path);

  return fallback_path;
}

char *get_status_reason(int code) {
  switch (code) {
  case 100:
    return "Continue";
  case 101:
    return "Switching Protocols";
  case 102:
    return "Processing";
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 203:
    return "Non-Authoritative Information";
  case 204:
    return "No Content";
  case 205:
    return "Reset Content";
  case 206:
    return "Partial Content";
  case 300:
    return "Multiple Choices";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Found";
  case 303:
    return "See Other";
  case 304:
    return "Not Modified";
  case 307:
    return "Temporary Redirect";
  case 308:
    return "Permanent Redirect";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 402:
    return "Payment Required";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 406:
    return "Not Acceptable";
  case 408:
    return "Request Timeout";
  case 409:
    return "Conflict";
  case 410:
    return "Gone";
  case 411:
    return "Length Required";
  case 413:
    return "Payload Too Large";
  case 414:
    return "URI Too Long";
  case 415:
    return "Unsupported Media Type";
  case 426:
    return "Upgrade Required";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 502:
    return "Bad Gateway";
  case 503:
    return "Service Unavailable";
  case 504:
    return "Gateway Timeout";
  case 505:
    return "HTTP Version Not Supported";
  default:
    return "Unknown Status";
  }
}

char *http_date_now() {
  time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  char *buf = malloc(30);
  if (!buf)
    return NULL;
  strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT", tm);
  return buf;
}

char *http_last_modified(const char *path) {
  struct stat st;
  if (stat(path, &st) < 0)
    return NULL;

  struct tm *tm = gmtime(&st.st_mtime);
  char *buf = malloc(30);
  if (!buf)
    return NULL;
  strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT", tm);
  return buf;
}

// Helper to trim trailing CRLF and spaces
void trim_crlf(char *line) {
  int len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                     isspace(line[len - 1]))) {
    line[len - 1] = '\0';
    len--;
  }
}

// caller must free only success as this automatically frees on failure
HttpRequest *parse_request(const char *raw_request) {
  if (!raw_request)
    return NULL;

  HttpRequest *req = malloc(sizeof(HttpRequest));
  if (!req)
    return NULL;
  memset(req, 0, sizeof(HttpRequest));

  // We'll copy raw_request so we can tokenize it safely
  char *buffer = strdup(raw_request);
  if (!buffer) {
    free(req);
    return NULL;
  }

  char *line = buffer;
  char *next_line;

  // Parse request line (first line)
  next_line = strstr(line, "\r\n");
  if (!next_line) {
    // maybe just '\n'
    next_line = strchr(line, '\n');
    if (!next_line)
      goto error;
  }
  *next_line = '\0';

  // tokenize request line: METHOD PATH VERSION
  char *method = strtok(line, " ");
  char *path = strtok(NULL, " ");
  char *version = strtok(NULL, " ");
  if (!method || !path || !version)
    goto error;

  req->request_line.method = strdup(method);
  req->request_line.path = strdup(path);
  req->request_line.version = strdup(version);

  if (!req->request_line.method || !req->request_line.path ||
      !req->request_line.version)
    goto error;

  // Move to next line (headers start here)
  line = next_line + 2; // skip \r\n
  if (*line == '\n')
    line++; // handle \r\n or \n

  // Parse headers
  req->headers = malloc(sizeof(Header) * MAX_HEADERS);
  if (!req->headers)
    goto error;
  req->header_count = 0;

  while (*line && req->header_count < MAX_HEADERS) {
    next_line = strstr(line, "\r\n");
    if (!next_line)
      next_line = strchr(line, '\n');
    if (!next_line)
      break;

    *next_line = '\0';
    trim_crlf(line);

    // Empty line means end of headers
    if (strlen(line) == 0)
      break;

    // Split header into key and value by ':'
    char *colon = strchr(line, ':');
    if (!colon)
      goto error;

    *colon = '\0';
    char *key = line;
    char *value = colon + 1;

    // trim spaces from value
    while (*value == ' ')
      value++;

    req->headers[req->header_count].key = strdup(key);
    req->headers[req->header_count].value = strdup(value);
    if (!req->headers[req->header_count].key ||
        !req->headers[req->header_count].value)
      goto error;

    req->header_count++;

    line = next_line + 2;
    if (*line == '\n')
      line++; // handle \r\n or \n
  }

  free(buffer);
  return req;

error:
  if (buffer)
    free(buffer);
  if (req) {
    // free partially allocated fields
    if (req->request_line.method)
      free(req->request_line.method);
    if (req->request_line.path)
      free(req->request_line.path);
    if (req->request_line.version)
      free(req->request_line.version);
    for (size_t i = 0; i < req->header_count; i++) {
      free(req->headers[i].key);
      free(req->headers[i].value);
    }
    free(req->headers);
    free(req);
  }
  return NULL;
}

HttpResponse *parse_response(const char *raw_response) {
  if (!raw_response)
    return NULL;

  HttpResponse *response = malloc(sizeof(HttpResponse));
  if (!response)
    return NULL;

  memset(response, 0, sizeof(HttpResponse));

  const char *ptr = raw_response;
  const char *line_end;

  // 1. Parse status line
  line_end = strstr(ptr, "\r\n");
  if (!line_end) {
    free(response);
    return NULL;
  }

  size_t status_line_len = line_end - ptr;
  char *status_line = malloc(status_line_len + 1);
  if (!status_line) {
    free(response);
    return NULL;
  }
  memcpy(status_line, ptr, status_line_len);
  status_line[status_line_len] = '\0';

  char *token = strtok(status_line, " ");
  if (!token) goto fail_status;
  response->status_line.http_version = strdup(token);

  token = strtok(NULL, " ");
  if (!token) goto fail_status;
  response->status_line.status_code = atoi(token);

  token = strtok(NULL, "\r\n");
  response->status_line.status_reason = strdup(token ? token : "");

  free(status_line);
  ptr = line_end + 2;

  // 2. Parse headers
  size_t headers_capacity = 10;
  response->headers = malloc(sizeof(Header) * headers_capacity);
  response->header_count = 0;

  while (true) {
    line_end = strstr(ptr, "\r\n");
    if (!line_end) break;

    if (line_end == ptr) {
      ptr += 2; // blank line, end of headers
      break;
    }

    size_t line_len = line_end - ptr;
    char *header_line = malloc(line_len + 1);
    if (!header_line) break;
    memcpy(header_line, ptr, line_len);
    header_line[line_len] = '\0';

    char *colon = strchr(header_line, ':');
    if (!colon) {
      free(header_line);
      break;
    }

    *colon = '\0';
    char *key = header_line;
    char *value = colon + 1;
    while (isspace((unsigned char)*value)) value++;

    if (response->header_count >= headers_capacity) {
      headers_capacity *= 2;
      Header *tmp = realloc(response->headers, sizeof(Header) * headers_capacity);
      if (!tmp) {
        free(header_line);
        break;
      }
      response->headers = tmp;
    }

    response->headers[response->header_count].key = strdup(key);
    response->headers[response->header_count].value = strdup(value);
    response->header_count++;

    free(header_line);
    ptr = line_end + 2;
  }

  // 3. Parse body
  const char *body_ptr = ptr;
  size_t body_len = strlen(body_ptr);

  // Check for Content-Length header
  for (size_t i = 0; i < response->header_count; i++) {
    if (strcasecmp(response->headers[i].key, "Content-Length") == 0) {
      body_len = (size_t)atoi(response->headers[i].value);
      break;
    }
  }

  return response;

fail_status:
  free(status_line);
  free(response);
  return NULL;
}

void set_header(HttpResponse *res, const char *key, const char *val) {
  for (size_t i = 0; i < res->header_count; i++) {
    if (strcmp(res->headers[i].key, key) == 0) {
      free(res->headers[i].value);
      res->headers[i].value = strdup(val);
      return;
    }
  }
}

void free_response(HttpResponse *response) {
  if (!response)
    return;

  // free status line strings
  if (response->status_line.http_version) {
    free(response->status_line.http_version);
  }
  if (response->status_line.status_reason) {
    free(response->status_line.status_reason);
  }

  // free each header key and value
  for (size_t i = 0; i < response->header_count; i++) {
    if (response->headers[i].key) {
      free(response->headers[i].key);
    }
    if (response->headers[i].value) {
      free(response->headers[i].value);
    }
  }

  // free headers array itself
  if (response->headers) {
    free(response->headers);
  }

  // free body if allocated dynamically
  // important to only free body if memory is owned so prolly use strdup always

  free(response);
}

void free_request(HttpRequest *request) {
  if (!request)
    return;

  // free request line strings
  if (request->request_line.method) {
    free(request->request_line.method);
  }
  if (request->request_line.path) {
    free(request->request_line.path);
  }
  if (request->request_line.version) {
    free(request->request_line.version);
  }

  // free each header key and value
  for (size_t i = 0; i < request->header_count; i++) {
    if (request->headers[i].key) {
      free(request->headers[i].key);
    }
    if (request->headers[i].value) {
      free(request->headers[i].value);
    }
  }

  // free headers array itself
  if (request->headers) {
    free(request->headers);
  }

  // finally free the HttpRequest struct itself
  free(request);
}
