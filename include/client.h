#ifndef client_h
#define client_h

#include "config.h"
#include "proxy.h"
#include "utils_http.h"
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_BUFFER_SIZE 8192

typedef struct {
  int fd;
  char read_buffer[MAX_BUFFER_SIZE];
  size_t read_buffer_len;
  HttpRequest *request;
  HttpResponse *response;
  char *response_headers_buffer;
  size_t response_headers_len;
  size_t response_headers_sent;
  off_t file_send_offset; // for sendfile
  
  // --- proxy specific members ---
  int backend_fd;         // file descriptor for the backend server connection
  char backend_read_buffer[MAX_BUFFER_SIZE]; // buffer for reading from backend
  size_t backend_read_buffer_len;
  char *backend_write_buffer; // buffer for writing to backend which is just the original client request
  size_t backend_write_buffer_len;
  size_t backend_write_buffer_sent;

  struct sockaddr_in backend_addr; // address of the backend server
  // i might need more state for DNS resolution if backend is hostname and connection establishment status

  enum {
    STATE_READING_REQUEST,
    STATE_PROCESSING_REQUEST,   // this is where to initiate backend connection
    STATE_CONNECTING_BACKEND,
    STATE_SENDING_TO_BACKEND,
    STATE_READING_FROM_BACKEND, 
    STATE_SENDING_HEADERS,
    STATE_SENDING_BODY,
    STATE_CLOSING
  } state;

  ProxyResult *proxy_result; // To temporarily hold headers/body for sending to client
} ClientState;

#endif /* client_h */
