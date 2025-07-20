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

#endif /* utils_http_h */
