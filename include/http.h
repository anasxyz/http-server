#ifndef http_h
#define http_h

#include <netinet/in.h>

typedef struct {
  char *key;
  char *value;
} Header;

typedef struct {
  char *status;
  char *content_type;
  char *body;
  size_t body_length;
  Header *headers;
  size_t num_headers;
} HttpResponse;

void send_response(int socket, HttpResponse *response);
void handle_request(int socket, char *request);

#endif /* http_h */
