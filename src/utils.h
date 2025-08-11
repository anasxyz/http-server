#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>
#include <time.h>
#include <glib.h>
#include <sys/epoll.h> 

// Function to set a file descriptor (like a socket) to non-blocking mode.
int set_nonblocking(int fd);

#endif // UTILS_H
