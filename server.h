#ifndef server_h
#define server_h

#include <sys/socket.h>
#include <netinet/in.h>

struct Server {
  int domain; // address domain, AF_INET for ipv4
  int service; // type of service, SOCK_STREAM for tcp
  int protocol; // 0 means default protocol for given domain and service
  u_long interface; // IP address to bind to, INADDR_ANY to accept connections from any IP
  int port; // port number to bind socket to
  int backlog; // maximum number of queued connections

  struct sockaddr_in address;

  int socket;

  void (*launch)(void);
};

struct Server server_constructor(int domain, int service, int protocol, u_long interface, int port, int backlog, void (*launch)(void));

#endif /* server_h */
