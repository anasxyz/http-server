#include "server.h"
#include <stdio.h>
#include <stdlib.h>

struct Server server_constructor(
    int domain,       // address domain, AF_INET for ipv4
    int service,      // type of service, SOCK_STREAM for tcp
    int protocol,     // 0 means default protocol for given domain and service
    u_long interface, // IP address to bind to, INADDR_ANY to accept connections from any IP
    int port,         // port number to bind socket to
    int backlog,      // maximum number of queued connections
    void (*launch)(struct Server *server) // pointer to function that will launch the server
) {

  struct Server server; // server struct holding all server info

  // basic config fields
  server.domain = domain;
  server.service = service;
  server.protocol = protocol;
  server.interface = interface;
  server.port = port;
  server.backlog = backlog;

  // prepare sockaddr_in address structure
  // sin_family: specifies address family, AF_INET for ipv4
  // sin_port: specifies port number, htons(port) converts port number to
  // network byte order sin_addr.s_addr: specifies IP address, htonl(interface) converts IP address to network byte order
  server.address.sin_family = domain;
  server.address.sin_port = htons(port);
  server.address.sin_addr.s_addr = htonl(interface);

  // create server socket using socket() system call
  // returns a file descriptor for the new socket if successful, otherwise -1
  server.socket = socket(domain, protocol, service);

  // if socket creation failed, print error message and exit
  if (server.socket == 0) {
    perror("Failed to connect to socket...\n");
    exit(1);
  }

  // bind socket to IP address and port number specified in server.address
  // bind() connects socket to local address structure
  if ((bind(server.socket, (struct sockaddr *)&server.address, sizeof(server.address)) < 0)) {
    perror("Failed to bind socket...\n");
    exit(1);
  }

  // tell socket to listen for incoming connections
  // backlog specifies the maximum length to which the queue of pending connections for sockfd may grow
  if (listen(server.socket, server.backlog) < 0) {
    perror("Failed to start listening...\n");
    exit(1);
  }

  server.launch = launch;

  // return fully configured server struct
  return server;
}
