
#include "utils_http.h"
#include <stddef.h>
#include <stdio.h>
#define MAX_BUFFER_SIZE 8192 // Or a larger size based on your needs

typedef struct {
    int fd;
    char read_buffer[MAX_BUFFER_SIZE];
    size_t read_buffer_len;
    HttpRequest *request;
    HttpResponse *response;
    char *response_headers_buffer;
    size_t response_headers_len;
    size_t response_headers_sent;
    off_t file_send_offset; // For sendfile
    enum {
        STATE_READING_REQUEST,
        STATE_PROCESSING_REQUEST,
        STATE_SENDING_HEADERS,
        STATE_SENDING_BODY,
        STATE_CLOSING
    } state;
} ClientState;

