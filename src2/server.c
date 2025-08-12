#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "util.h"

int setup_listening_socket(int port) {
  int listen_sock;
  struct sockaddr_in server_addr;
  int opt = 1;

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == -1) {
    logs('E', "Failed to create a listening socket. The server cannot start.",
         "socket creation failed");
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    logs('E', "Failed to configure socket options. The server cannot start.",
         "setsockopt SO_REUSEADDR failed");
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    fprintf(stderr,
            "ERROR: Failed to bind the socket to port %d. The port may be in "
            "use.\n",
            port);
#ifdef VERBOSE_MODE
    perror("REASON: bind failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    fprintf(stderr,
            "ERROR: Failed to prepare the socket for incoming connections.\n");
#ifdef VERBOSE_MODE
    perror("REASON: listen failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    fprintf(stderr, "ERROR: Failed to configure the listening socket for "
                    "non-blocking operations.\n");
    // The reason is already printed in set_nonblocking
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}
