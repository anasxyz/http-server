// main.c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h> // For fcntl
#include <sys/epoll.h> // For epoll
#include <errno.h> // For errno

#include "../include/config.h"
#include "../include/server.h" // server_constructor, struct Server
#include "../include/event_handler.h" // NEW: For G_epoll_fd, handle_epoll_event, check_keep_alive_timeouts

// Global epoll file descriptor (declared extern in event_handler.h, defined in event_handler.c)
extern int G_epoll_fd;

void handle_sigint() {
  printf("\nCaught SIGINT, cleaning up...\n");
  free_config();
  // Close epoll_fd and all client sockets gracefully here
  // Iterate through active_connections and remove_connection for each
  if (G_epoll_fd != -1) {
      close(G_epoll_fd);
  }
  // TODO: Add logic to clean up all active_connections
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
      if (active_connections[i] != NULL) {
          remove_connection(active_connections[i]); // This frees and closes
      }
  }
  exit(0);
}

void launch(struct Server *server) {
  printf("======== SERVER STARTED ========\n");
  printf("Server listening on http://localhost:%d\n", server->port);
  printf("================================\n");

  // 1. Set the listening socket to non-blocking
  if (fcntl(server->socket, F_SETFL, O_NONBLOCK) == -1) {
      perror("Failed to set listening socket to non-blocking");
      exit(EXIT_FAILURE);
  }

  // 2. Create the epoll instance
  G_epoll_fd = epoll_create1(0); // 0 for default flags
  if (G_epoll_fd == -1) {
      perror("Failed to create epoll file descriptor");
      exit(EXIT_FAILURE);
  }

  // 3. Add the listening socket to epoll
  struct epoll_event event;
  event.events = EPOLLIN | EPOLLET; // EPOLLIN for read events, EPOLLET for edge-triggered
  event.data.fd = server->socket; // Associate the listening socket FD
  if (epoll_ctl(G_epoll_fd, EPOLL_CTL_ADD, server->socket, &event) == -1) {
      perror("Failed to add listening socket to epoll");
      close(G_epoll_fd);
      exit(EXIT_FAILURE);
  }

  // Main event loop
  const int MAX_EPOLL_EVENTS = 100; // Max events to retrieve per epoll_wait call
  struct epoll_event events[MAX_EPOLL_EVENTS];

  while (1) {
      int num_events = epoll_wait(G_epoll_fd, events, MAX_EPOLL_EVENTS, 1000); // 1000ms timeout
      if (num_events == -1) {
          if (errno == EINTR) continue; // Interrupted by signal, just retry
          perror("epoll_wait failed");
          break; // Exit loop on critical error
      }

      for (int i = 0; i < num_events; i++) {
          handle_epoll_event(&events[i], server->socket); // Call the event handler
      }

      check_keep_alive_timeouts(); // Check for timeouts
  }

  // Cleanup if loop breaks (e.g., epoll_wait error)
  close(G_epoll_fd);
  // TODO: Add logic to clean up all active_connections here as well
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
      if (active_connections[i] != NULL) {
          remove_connection(active_connections[i]);
      }
  }
}

int main() {
  signal(SIGINT, handle_sigint);
  load_config("server.conf");
  struct Server server = server_constructor(AF_INET, SOCK_STREAM, 0, INADDR_ANY, PORT, 10, launch);
  server.launch(&server);
  free_config();
  return 0;
}
