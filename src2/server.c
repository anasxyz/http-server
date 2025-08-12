#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "util.h"

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return -1;
  }
  return 0;
}

int setup_listening_socket(int port) {
  int listen_sock;
  struct sockaddr_in server_addr;
  int opt = 1;

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == -1) {
    logs('E', "Failed to create a listening socket.",
         "setup_listening_socket(): socket creation failed.\n");
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    logs('E', "Failed to configure socket options.",
         "setup_listening_socket(): setsockopt() with SO_REUSEADDR failed.\n");
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    logs('E', "Failed to bind the socket to port %d.",
         "setup_listening_socket(): bind() failed, port may be in use.\n", port);
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    logs('E', "Failed to prepare the socket for incoming connections.",
         "setup_listening_socket(): listen() failed.\n");
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    logs('E', "Failed to configure socket to non-blocking.",
         "setup_listening_socket(): set_nonblocking() failed.\n");
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}

int main() {
	logs('I', "Starting server...", NULL);

	int listen_sock = setup_listening_socket(8080);
	return 0;
}
