#ifndef server_h
#define server_h

#include <sys/socket.h>
#include <netinet/in.h>

struct Server {
  int domain;
  int service;
  int protocol;
  u_long interfance;
  int port;
  int backlog;

  struct sockaddr_in address;

  void (*launch)(void);
};

struct Server server_constructor(int domain, int service, int protocol, u_long interface, int pot, int backlog, void (*launch)(void));

#endif /* server_h */
