#ifndef RESPONSE_HANDLER_H
#define RESPONSE_HANDLER_H

#include "server.h"

int handle_write_event(client_state_t *client_state, int epoll_fd);

#endif // RESPONSE_HANDLER_H
