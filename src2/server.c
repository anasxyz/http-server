#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

#define MAX_EVENTS 1024
#define MAX_BUFFER_SIZE 8192

typedef struct {
  char *key;
  char *value;
} header_t;

typedef struct {
  char *method;
  char *uri;
  char *version;
} request_line_t;

typedef struct {
  request_line_t request_line;
  header_t *headers;
  size_t num_headers;
  char *body;
  size_t body_len;
} request_t;

typedef struct {
  char *version;
  int status_code;
  char *status_message;
} response_line_t;

typedef struct {
  response_line_t response_line;
  header_t *headers;
  size_t num_headers;
} response_t;

typedef enum {
  READING,
  WRITING,
  IDLE,
  CLOSING,
} state_e;

typedef struct {
  int fd;
  char *ip;

  char in_buffer[MAX_BUFFER_SIZE];
  size_t in_buffer_len;

  char out_buffer[MAX_BUFFER_SIZE];
  size_t out_buffer_len;
  size_t out_buffer_sent;

  state_e state;

  request_t *request;
  response_t *response;
} client_t;

void exits() {
  fprintf(stderr, "EXIT: Error occured. Exiting.");
  exit(1);
}

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
         "setup_listening_socket(): socket creation failed.");
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    logs('E', "Failed to configure socket options.",
         "setup_listening_socket(): setsockopt() with SO_REUSEADDR failed.");
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
         "setup_listening_socket(): bind() failed, port may be in use.", port);
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    logs('E', "Failed to prepare the socket for incoming connections.",
         "setup_listening_socket(): listen() failed.");
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    logs('E', "Failed to configure socket to non-blocking.",
         "setup_listening_socket(): set_nonblocking() failed.");
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

void read_client_request(client_t *client) {
  char temp_buffer[MAX_BUFFER_SIZE];
  ssize_t bytes_read;

  // keep reading until we get a full request
  while (1) {
    // read the raw request into temporary buffer
    bytes_read = read(client->fd, temp_buffer, MAX_BUFFER_SIZE);

    // if read() return 0, it means the client closed the connection
    if (bytes_read == 0) {
      client->state = CLOSING;
      logs('I', "Client %s disconnected.", NULL, client->ip);
      break;
    }

    // if read() returns -1, it means there's an error
    if (bytes_read == -1) {
			// if error is EAGAIN or EWOULDBLOCK, it means that there is no more data to read right now
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        logs('D', "No more data to read from Client %s", NULL, client->ip);
        break;
      } else {
				// a real error occured, so handle it
				client->state = CLOSING;
			}
    }

    // if we got this far, this means that bytes_read > 0
    // so add the new data to client's in_buffer
		// check for buffer overflow first before copying data
		// if new data exceeds fixed size buffer then treat as error
    if (client->in_buffer_len + bytes_read >= MAX_BUFFER_SIZE) {
			client->state = CLOSING;
		}

		// copy new data from temp buffer into client main buffer
		memcpy(client->in_buffer + client->in_buffer_len, temp_buffer, bytes_read);
		client->in_buffer_len += bytes_read;

		// null terminate for easier parsing late
		client->in_buffer[client->in_buffer_len] = '\0';
  }
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
    char *file_path = "/var/www/index.html";
    struct stat st;
    long file_size;
    int file_fd;

    // Open the file using the low-level open() to get an integer file
    // descriptor
    file_fd = open(file_path, O_RDONLY);
    if (file_fd == -1) {
      logs('E', "Failed to open file %s.", "handle_client(): open() failed.",
           file_path);
      // Send a 404 Not Found response
      const char *not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                       "Content-Length: 0\r\n"
                                       "\r\n";
      write_response(client_sock, not_found_response);
      return;
    }

    // Get the file size using fstat, which is more robust than fseek/ftell
    if (fstat(file_fd, &st) == -1) {
      logs('E', "Failed to get file size for %s.",
           "handle_client(): fstat() failed.", file_path);
      close(file_fd);
      return;
    }
    file_size = st.st_size;

    // Create the HTTP response headers with the correct file size
    const char *http_response_template = "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/html\r\n"
                                         "Content-Length: %zu\r\n"
                                         "\r\n";

    char headers[1024];
    snprintf(headers, sizeof(headers), http_response_template, file_size);

    // Send the HTTP headers first
    write_response(client_sock, headers);

    // Then, send the file content using sendfile
    sendfile(client_sock, file_fd, NULL, file_size);

    // Close the file descriptor
    close(file_fd);
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

  // set client's socket to non-blocking
  set_nonblocking(client_sock);

	// allocate and initialise new client struct for this connection
	client_t *client = malloc(sizeof(client_t));
	client->fd = client_sock;
	client->ip = strdup(inet_ntoa(client_address.sin_addr));
	memset(client->in_buffer, 0, MAX_BUFFER_SIZE);
	client-> in_buffer_len = 0;
	memset(client->out_buffer, 0, MAX_BUFFER_SIZE);
	client->out_buffer_len = 0;
	client->out_buffer_sent = 0;
	client->state = IDLE;
	client->request = NULL;
	client->response = NULL;

  // add client's socket to epoll instance
  event.events = EPOLLIN;
  event.data.fd = client->fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client->fd, &event) == -1) {
    logs('E', "Failed to add client socket to epoll.",
         "handle_new_connection(): epoll_ctl() failed.");
    close(client->fd);
    return -1;
  }

  logs('I', "Accepted connection %s (socket %d).", NULL, client->ip,
       client->fd);

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
    exits();
  }

  // add the listening sockets to the epoll instance
  for (i = 0; i < num_sockets; i++) {
    event.events = EPOLLIN;
    event.data.fd = listen_sockets[i];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sockets[i], &event) == -1) {
      logs('E', "Worker failed to register listening socket.",
           "run_worker(): epoll_ctl() failed.");
      close(epoll_fd);
      exits();
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
      exits();
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
    logs('I', "%s listening on port %d.", NULL, servers[i].server_name,
         servers[i].listen_port);
    if (listen_sockets[i] == -1) {
      exits();
    }
  }
}

void fork_workers(int *listen_sockets) {
  // fork worker processes
  for (int i = 0; i < global_config->worker_processes; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      logs('E', "Failed to fork worker process.", NULL);
      exits();
    }
    // child process
    if (pid == 0) {
      logs('I', "Worker process %d started", NULL, i);
      run_worker(listen_sockets, global_config->http->num_servers);
      exits();
    }
  }
}

void server_cleanup(int *listen_sockets) {
  // master process waits for all childen to finish
  // TODO: handle signals
  for (int i = 0; i < global_config->worker_processes; i++) {
    wait(NULL);
  }

  // clean up
  for (int i = 0; i < global_config->http->num_servers; i++) {
    close(listen_sockets[i]);
  }
}

void check_valid_config() {
  server_config *servers = global_config->http->servers;
  int num_servers = global_config->http->num_servers;

  // --- global config checks ---
  if (num_servers == 0) {
    logs('E', "No servers configured.", NULL);
    exits();
  }
  if (global_config->worker_processes == 0) {
    logs('E', "No worker processes configured.", NULL);
    exits();
  }

  // --- server config checks ---
  for (int i = 0; i < num_servers; i++) {
    if (!servers[i].server_name) {
      char default_name[32];
      snprintf(default_name, sizeof(default_name), "server%d", i + 1);
      servers[i].server_name = strdup(default_name);
      logs('W', "No name configured for server %d. Setting to default: %s",
           NULL, i + 1, default_name);
    }

    if (!servers[i].listen_port) {
      logs('E', "No port configured for server %s", NULL,
           servers[i].server_name);
      exits();
    }
    if (servers[i].listen_port < 1024) {
      logs('E',
           "Invalid port number. Configure a different port for %s (>1024).",
           NULL, servers[i].server_name);
      exits();
    }
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
