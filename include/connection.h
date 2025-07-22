#ifndef connection_h
#define connection_h

#include "utils_http.h"
#include <sys/socket.h> 
#include <time.h>     

#define INITIAL_READ_BUFFER_SIZE 4096 // A reasonable starting size for the read buffer

// Define states for your connection
typedef enum {
    CONN_STATE_READING_REQUEST,  // currently reading request headers/body
    CONN_STATE_SENDING_RESPONSE, // currently sending response headers/body
    CONN_STATE_KEEP_ALIVE,       // waiting for next request on persistent connection
    CONN_STATE_CLOSING           // marked for closing (e.g. after timeout or explicit close)
} ConnectionState;

typedef struct {
    int fd;                       // socket file descriptor
    ConnectionState state;        // current state of the connection
    char *read_buffer;            // buffer for incoming request data
    size_t read_buffer_size;      // total size of the buffer
    size_t bytes_read;            // how many bytes are currently in the buffer
    char *write_buffer;           // buffer for outgoing response data (if not using sendfile)
    size_t write_buffer_size;
    size_t bytes_sent;            // how many bytes of the response have been sent

    time_t last_activity_time;    // timestamp of last read/write activity
    int keep_alive_timeout;       // configured timeout for this connection (e.g. 60 seconds)
    int keep_alive_max_requests;  // max requests for this connection (e.g. 100)
    int current_requests_served;  // counter for requests served on this connection

    // pointers to request/response objects
    HttpRequest *current_request;
    HttpResponse *current_response;

    // add other fields maybe for multipart reads, response body parts, etc.

} Connection;

// functions to manage connections (alloc, free, update)
Connection *connection_create(int fd);
void connection_reset(Connection *conn); // resets state for a new request on same connection
void connection_destroy(Connection *conn);

#endif // connection_h
