// include/event_handler.h
#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include "connection.h" // For Connection struct
#include <sys/epoll.h> // For epoll

// Declare the global epoll file descriptor
extern int G_epoll_fd;

// Declare the global array for active connections
#define MAX_CONNECTIONS 1024 // Define MAX_CONNECTIONS here
extern Connection *active_connections[MAX_CONNECTIONS];

// Declare helper functions for managing connections
Connection *get_connection_by_fd(int fd);
void add_connection(Connection *conn);
void remove_connection(Connection *conn);

// Declare the main event handling function
void handle_epoll_event(struct epoll_event *event, int listen_fd);

// Declare the keep-alive timeout checker
void check_keep_alive_timeouts();

// Declare the function that processes a full HTTP request
void process_full_request(Connection *conn, int epoll_fd);

#endif // EVENT_HANDLER_H
