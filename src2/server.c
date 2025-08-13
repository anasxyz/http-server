#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

#define MAX_EVENTS 1024

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
         "setup_listening_socket(): bind() failed, port may be in use.\n",
         port);
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

// Reads a request from a client socket into a buffer.
int read_request(int sock, char *buffer, size_t buf_size) {
  memset(buffer, 0, buf_size); // Clear the buffer
  int bytes_read = read(sock, buffer, buf_size - 1);
  if (bytes_read <= 0) {
    return bytes_read;
  }
  buffer[bytes_read] = '\0'; // Null-terminate the string
  return bytes_read;
}

// Writes a hardcoded response to the client socket.
int write_response(int sock, const char *response_msg) {
  int bytes_written = write(sock, response_msg, strlen(response_msg));
  if (bytes_written < 0) {
    logs('E', "Failed to write response to client.",
         "write_response(): write() failed.");
  }
  return bytes_written;
}

// Handles a single client connection.
void handle_client(int client_sock) {
  char buffer[1024];

  // Read the dummy request
  if (read_request(client_sock, buffer, 1024) > 0) {
    // Create a simple, hardcoded HTTP response
    const char *response_body = "<h1>Hello from your simple server!</h1>";
    const char *http_response_template = "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/html\r\n"
                                         "Content-Length: %zu\r\n"
                                         "\r\n"
                                         "%s";

    char full_response[1024];
    snprintf(full_response, sizeof(full_response), http_response_template,
             strlen(response_body), response_body);

    // Write the dummy response
    write_response(client_sock, full_response);
  }
}

int handle_new_connection(int connection_socket, int epoll_fd) {
  struct sockaddr_in client_address;
  socklen_t client_address_len;
  struct epoll_event event;
  client_address_len = sizeof(client_address);

  // accept client's connection
  int client_sock =
      accept(connection_socket, (struct sockaddr *)&client_address,
             &client_address_len);
  if (client_sock == -1) {
    logs('E', "Failed to accept connection.",
         "handle_new_connection(): accept() failed.");
    return -1;
  }

  // convert the client's IP address from network to a string
  char *client_ip = inet_ntoa(client_address.sin_addr);

  // set client's socket to non-blocking
  set_nonblocking(client_sock);

  // add client's socket to epoll instance
  event.events = EPOLLIN;
  event.data.fd = client_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &event) == -1) {
    logs('E', "Failed to add client socket to epoll.",
         "handle_new_connection(): epoll_ctl() failed.");
    close(client_sock);
    return -1;
  }

  logs('I', "Accepted connection %s (socket %d).", NULL, client_ip,
       client_sock);

  return 0;
}

void run_worker(int *listen_sockets, int num_sockets) {
  // --- Start epoll setup ---
  int epoll_fd;
  struct epoll_event event;
  struct epoll_event events[MAX_EVENTS];
  int num_events;
  int i;

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    logs('E', "Worker failed to create epoll instance.",
         "run_worker(): epoll_create1() failed.");
    exit(1);
  }

  // add the listening sockets to the epoll instance
  for (i = 0; i < num_sockets; i++) {
    event.events = EPOLLIN;
    event.data.fd = listen_sockets[i];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sockets[i], &event) == -1) {
      logs('E', "Worker failed to register listening socket.",
           "run_worker(): epoll_ctl() failed.");
      close(epoll_fd);
      exit(1);
    }
  }
  // --- End epoll setup ---

  while (1) {
    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events == -1) {
      if (errno == EINTR) {
        continue;
      }
      logs('E', "Worker's epoll_wait failed.",
           "run_worker(): epoll_wait() failed.");
      exit(1);
    }

    for (int i = 0; i < num_events; i++) {
      int current_fd = events[i].data.fd;

      // check if event is on a listening socket
      int is_listening_socket = 0;
      for (int j = 0; j < num_sockets; j++) {
        if (current_fd == listen_sockets[j]) {
          is_listening_socket = 1;
          break;
        }
      }

      if (is_listening_socket) {
        // handle new connection
        handle_new_connection(current_fd, epoll_fd);
      } else {
        // handle client event
        handle_client(current_fd);
      }
    }
  }
}

void init_sockets(int *listen_sockets) {
  server_config *servers = global_config->http->servers;

  for (int i = 0; i < global_config->http->num_servers; i++) {
    listen_sockets[i] = setup_listening_socket(servers[i].listen_port);
    logs('I', "Listening on port %d.", NULL, servers[i].listen_port);
    if (listen_sockets[i] == -1) {
      exit(1);
    }
  }
}

void fork_workers(int *listen_sockets) {
  // fork worker processes
  for (int i = 0; i < global_config->worker_processes; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      logs('E', "Failed to fork worker process.", NULL);
      exit(1);
    }
    // child process
    if (pid == 0) {
      logs('I', "Worker process %d started", NULL, i);
      run_worker(listen_sockets, global_config->http->num_servers);
      exit(0);
    }
  }
}

void server_cleanup(int *listen_sockets) {
  // master process waits for all childen to finish
  // TODO: handle signals
  logs('I', "Master process is waiting for workers to finish.", NULL);
  for (int i = 0; i < global_config->worker_processes; i++) {
    wait(NULL);
  }

  // clean up
  for (int i = 0; i < global_config->http->num_servers; i++) {
    close(listen_sockets[i]);
  }
}

void check_valid_config() {
  if (global_config->http->num_servers == 0) {
    logs('E', "No servers configured.", NULL);
    exit(1);
  }
  if (global_config->worker_processes == 0) {
    logs('E', "No worker processes configured.", NULL);
    exit(1);
  }
}

int main() {
  logs('I', "Starting server...", NULL);

  // init config
  init_config();

	// TODO: parse config (leave for now)
	// parse_config();

  // check if config is valid
	check_valid_config();

  // setup listening sockets
  int listen_sockets[global_config->http->num_servers];

	// fill socket array with server block sockets
  init_sockets(listen_sockets);

	// fork worker processes
  fork_workers(listen_sockets);

	// clean up server resources
  server_cleanup(listen_sockets);

  // TODO: free_config here when it's implemented
	// free_config();

  return 0;
}
