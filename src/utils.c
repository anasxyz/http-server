#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server.h"
#include "utils.h"

// Function to set a file descriptor (like a socket) to non-blocking mode.
int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    fprintf(stderr,
            "ERROR: An internal server error occurred during socket setup.\n");
#ifdef VERBOSE_MODE
    perror("REASON: fcntl F_GETFL failed");
#endif
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    fprintf(stderr,
            "ERROR: An internal server error occurred during socket setup.\n");
#ifdef VERBOSE_MODE
    perror("REASON: fcntl F_SETFL O_NONBLOCK failed");
#endif
    return -1;
  }
  return 0;
}

