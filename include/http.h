#ifndef http_h
#define http_h

#include <netinet/in.h>

typedef struct {
  char *key;
  char *value;
} Header;


typedef struct {
  int code;
  const char* reason;
} HttpStatus; 

typedef struct {
  HttpStatus status;
  char *content_type;
  char *body;
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

void send_response(int socket, HttpStatusCode status_code, const char* text);

void handle_request(int socket, char *request_buffer);
HttpRequest parse_request(char *request_buffer);

#endif /* http_h */
