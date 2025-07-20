#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../include/utils_http.h"

#define MAX_HEADERS 100
#define MAX_PATH 1024

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
    for (int i = 0; i < req->header_count; i++) {
      free(req->headers[i].key);
      free(req->headers[i].value);
    }
    free(req->headers);
    free(req);
  }
  return NULL;
}

void free_response(HttpResponse *response) {
  if (!response)
    return;

  // Free status line strings
  if (response->status_line.http_version) {
    free(response->status_line.http_version);
  }
  if (response->status_line.status_reason) {
    free(response->status_line.status_reason);
  }

  // Free each header key and value
  for (int i = 0; i < response->header_count; i++) {
    if (response->headers[i].key) {
      free(response->headers[i].key);
    }
    if (response->headers[i].value) {
      free(response->headers[i].value);
    }
  }

  // Free headers array itself
  if (response->headers) {
    free(response->headers);
  }

  // Free body if allocated dynamically
  // IMPORTANT: Only free if you own the memory!
  if (response->body) {
    free((void *)response->body);
  }

  // Finally free the HttpResponse struct itself
  free(response);
}

void free_request(HttpRequest *request) {
    if (!request) return;

    // Free request line strings
    if (request->request_line.method) {
        free(request->request_line.method);
    }
    if (request->request_line.path) {
        free(request->request_line.path);
    }
    if (request->request_line.version) {
        free(request->request_line.version);
    }

    // Free each header key and value
    for (int i = 0; i < request->header_count; i++) {
        if (request->headers[i].key) {
            free(request->headers[i].key);
        }
        if (request->headers[i].value) {
            free(request->headers[i].value);
        }
    }

    // Free headers array itself
    if (request->headers) {
        free(request->headers);
    }

    // Finally free the HttpRequest struct itself
    free(request);
}

