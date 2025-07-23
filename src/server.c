#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/server.h"

struct Server server_constructor(
    int domain,       // address domain, AF_INET for ipv4
    int service,      // type of service, SOCK_STREAM for tcp
    int protocol,     // 0 means default protocol for given domain and service
    u_long interface, // IP address to bind to, INADDR_ANY to accept connections from any IP
    int port,         // port number to bind socket to
    int backlog,      // maximum number of queued connections
    void (*launch)(struct Server *server) // pointer to function that will launch the server
) {
  struct Server server;

  server.domain = domain;
  server.service = service;
  server.protocol = protocol;
  server.interface = interface;
  server.port = port;
  server.backlog = backlog;
  server.launch = launch;

  // create socket
  server.socket = socket(domain, service, protocol);
  if (server.socket == -1) {
    perror("Failed to create socket");
    exit(1);
  }

  int opt = 1;
  if (setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    close(server.socket);
    exit(1);
  }

  // setup address
  memset(&server.address, 0, sizeof(server.address));
  server.address.sin_family = domain;
  server.address.sin_port = htons(port);
  server.address.sin_addr.s_addr = htonl(interface);

  // bind
  if (bind(server.socket, (struct sockaddr *)&server.address, sizeof(server.address)) < 0) {
    perror("Failed to bind socket");
    close(server.socket);
    exit(1);
  }

  // listen
  if (listen(server.socket, backlog) < 0) {
    perror("Failed to start listening");
    close(server.socket);
    exit(1);
  }

  return server;
}
