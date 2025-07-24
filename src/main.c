#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "../include/config.h"

#include "../include/client.h"
#include "../include/server.h"
#include "../include/http.h"

#define MAX_EVENTS 1000

void handle_sigint() {
  printf("\nCaught SIGINT, cleaning up...\n");
  free_config();
  exit(0);
}

void make_socket_non_blocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

void launch(struct Server *server) {
  printf("======== SERVER STARTED ========\n");
  printf("Server listening on http://localhost:%d\n", server->port);
  printf("================================\n");

  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  make_socket_non_blocking(server->socket);

  struct epoll_event event;
  event.data.fd = server->socket;
  event.events = EPOLLIN;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->socket, &event) < 0) {
    perror("epoll_ctl");
    exit(EXIT_FAILURE);
  }

  struct epoll_event events[MAX_EVENTS];

  while (1) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;

      if (fd == server->socket) {
        // new connection
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server->socket, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
          perror("accept");
          continue;
        }

        make_socket_non_blocking(client_fd);

        struct epoll_event client_event;
        client_event.data.fd = client_fd;
        client_event.events = EPOLLIN | EPOLLET;  // Edge-triggered
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
      } else {
        // existing client
        char buf[8192];
        int bytes_read = recv(fd, buf, sizeof(buf) - 1, 0);
        if (bytes_read <= 0) {
          close(fd);
          continue;
        }

        buf[bytes_read] = '\0';  // Null-terminate

        handle_request(fd, buf);

        close(fd);  // We close after response (you can make this persistent later)
      }
    }
  }
}

int main() {
  signal(SIGINT, handle_sigint);

  load_config("server.conf");

  struct Server server =
      server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, PORT, 10, launch);

  server.launch(&server);

  free_config();
}
