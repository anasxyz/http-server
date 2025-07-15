#ifndef http_h
#define http_h

#include <netinet/in.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct {
  const char* method;
  bool allowed;
} HttpMethod;

static const HttpMethod allowed_methods[] = {
  {"GET", true},
  {"POST", false},
  {"PUT", false},
  {"DELETE", false},
  {"PATCH", false},
  {"HEAD", false},
  {"OPTIONS", false},
  {"CONNECT", false},
  {"TRACE", false},
};

#define NUM_ALLOWED_METHODS (sizeof(allowed_methods) / sizeof(allowed_methods[0]))

typedef struct {
  char *key;
  char *value;
} Header;

typedef struct {
  int code;
  char *reason;
} HttpStatus;

typedef struct {
  char *status;
  char *date;
  char *server;
  char *last_modified;
  char *content_type;
  char *connection;

  const char *body;
  size_t body_length;
  Header *headers;
  size_t num_headers;
} HttpResponse;

typedef struct {
  char *method;
  char *path;
  char *version;
} HttpRequest;

typedef enum {
  HTTP_STATUS_OK = 200,
  HTTP_STATUS_BAD_REQUEST = 400,
  HTTP_STATUS_NOT_FOUND = 404,
  HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
  HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
} HttpStatusCode;

char *get_status_reason(int code);

HttpResponse *create_response(int status_code, const char *path);
void send_response(int socket, HttpResponse *response);

void handle_request(int socket, char *request_buffer);
HttpRequest parse_request(char *request_buffer);

#endif /* http_h */
