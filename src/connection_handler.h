#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include "server.h"

void handle_new_connection(int listen_sock, int epoll_fd, GHashTable *client_states_map, int *active_connections_ptr);
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr, GHashTable *client_states_map, int *active_connections_ptr);
void close_client_connection(int epoll_fd, client_state_t *client_state, GHashTable *client_states_map, int *active_connections_ptr);

#endif // CONNECTION_HANDLER_H
