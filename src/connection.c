// connection.c
#include "../include/connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 

Connection *connection_create(int fd) {
    Connection *conn = (Connection *)malloc(sizeof(Connection));
    if (!conn) {
        perror("Failed to allocate connection struct");
        return NULL;
    }
    memset(conn, 0, sizeof(Connection)); // Initialize all members to 0/NULL

    conn->fd = fd;
    conn->state = CONN_STATE_READING_REQUEST; // Default starting state
    conn->read_buffer_size = INITIAL_READ_BUFFER_SIZE;
    conn->read_buffer = (char *)malloc(conn->read_buffer_size);
    if (!conn->read_buffer) {
        perror("Failed to allocate read buffer for connection");
        free(conn);
        return NULL;
    }
    conn->bytes_read = 0;
    conn->read_buffer[0] = '\0'; // Ensure it's empty string initially

    // Default keep-alive settings (can be overridden by config)
    conn->keep_alive_timeout = 60; // 60 seconds
    conn->keep_alive_max_requests = 100; // 100 requests
    conn->current_requests_served = 0;

    return conn;
}

void connection_reset(Connection *conn) {
    if (!conn) return;

    // Free previous request/response if they exist
    if (conn->current_request) {
        free_request(conn->current_request); // Assuming free_request exists
        conn->current_request = NULL;
    }
    if (conn->current_response) {
        free_response(conn->current_response); // Assuming free_response exists
        conn->current_response = NULL;
    }

    // Reset read buffer for next request
    memset(conn->read_buffer, 0, conn->read_buffer_size);
    conn->bytes_read = 0;
    conn->read_buffer[0] = '\0';

    // Reset write buffer (if used)
    if (conn->write_buffer) {
        free(conn->write_buffer);
        conn->write_buffer = NULL;
        conn->write_buffer_size = 0;
    }
    conn->bytes_sent = 0;

    conn->state = CONN_STATE_READING_REQUEST; // Ready for next request
    conn->last_activity_time = time(NULL); // Update activity time
    // current_requests_served is NOT reset for keep-alive, it accumulates.
    // It's only reset when a new connection is established for that FD.
}

void connection_destroy(Connection *conn) {
    if (!conn) return;

    // Free buffers
    if (conn->read_buffer) {
        free(conn->read_buffer);
        conn->read_buffer = NULL;
    }
    if (conn->write_buffer) {
        free(conn->write_buffer);
        conn->write_buffer = NULL;
    }

    // Free any associated request/response structs
    if (conn->current_request) {
        free_request(conn->current_request);
    }
    if (conn->current_response) {
        free_response(conn->current_response);
    }

    free(conn); // Free the connection struct itself
}
