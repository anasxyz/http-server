#ifndef utils_http_h
#define utils_http_h

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *key;
  char *value;
} Header;

typedef struct {
  char* http_version;
  int status_code;
  char* status_reason;
} HttpResponseStatusLine;

typedef struct {
  HttpResponseStatusLine status_line;
  Header *headers;
  const char* body;
  int header_count;
} HttpResponse;

typedef struct {
  char *method;
  char *path;
  char *version;
} HttpRequestLine;

typedef struct {
  HttpRequestLine request_line;
  Header *headers;
  int header_count;
} HttpRequest;

typedef enum {
  HTTP_STATUS_OK = 200,
  HTTP_STATUS_BAD_REQUEST = 400,
  HTTP_STATUS_NOT_FOUND = 404,
  HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
  HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
} HttpStatusCode;

char *get_status_reason(int code);
char *http_date_now();
char *http_last_modified(const char *path);
void trim_crlf(char *line);
HttpRequest *parse_request(const char *raw_request);
HttpResponse *parse_response(const char *raw_response);
void free_response(HttpResponse *response);
void free_request(HttpRequest *request);

typedef HttpResponse *(*RequestHandler)(HttpRequest *, void *context);

typedef struct {
  const char *method;
  RequestHandler handler;
  void *context;  // for passing route info or other data
} MethodHandler;

#endif /* utils_http_h */
