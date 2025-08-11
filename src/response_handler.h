#ifndef RESPONSE_HANDLER_H
#define RESPONSE_HANDLER_H

#include "server.h"

int handle_write_event(client_state_t *client_state, int epoll_fd, GHashTable *client_states_map, int *active_connections_ptr);

#endif // RESPONSE_HANDLER_H
