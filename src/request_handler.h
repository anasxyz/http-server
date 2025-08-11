#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include "server.h"

int handle_read_event(client_state_t *client_state, int epoll_fd, GHashTable *client_states_map, int *active_connections_ptr);

#endif // REQUEST_HANDLER_H
