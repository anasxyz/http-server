#ifndef STATE_HANDLER_H
#define STATE_HANDLER_H

#include "server.h"

void transition_state(int epoll_fd, client_state_t *client, client_state_enum_t new_state);

#endif // STATE_HANDLER_H
