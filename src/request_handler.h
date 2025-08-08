#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include "server.h"

int handle_read_event(client_state_t *client_state, int epoll_fd);

#endif // REQUEST_HANDLER_H
