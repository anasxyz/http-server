#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "defaults.h"
#include "hashmap.h"
#include "mime.h"
#include "util.h"

#define MAX_EVENTS (2 * 1024)

atomic_int *total_connections;
long long request_count = 0;
int my_connections = 0;

volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t worker_running = 1;

void handle_signal(int sig) { g_running = 0; }

void worker_signal_handler(int sig) { worker_running = 0; }

typedef struct client client_t;

void close_connection(client_t *client);

void cleanup_shm() {
  const char *shm_name = "/server_connections";
  if (total_connections) {
    munmap(total_connections, sizeof(atomic_int));
    total_connections = NULL;
  }
  shm_unlink(shm_name);
}

void setup_total_connections() {
  const char *shm_name = "/server_connections";
  int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("ERROR: shm_open failed");
    exit(EXIT_FAILURE);
  }
  ftruncate(shm_fd, sizeof(atomic_int));
  total_connections = mmap(NULL, sizeof(atomic_int), PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
  if (total_connections == MAP_FAILED) {
    perror("ERROR: mmap failed");
    exit(EXIT_FAILURE);
  }
  atomic_store(total_connections, 0);
  atexit(cleanup_shm);
}

typedef struct timer_node timer_node_t;

typedef struct request {
  char method[10];
  char uri[512];
  char http_version[10];

  HashMap *headers;

  char *body_data;
  size_t body_len;
} request_t;

typedef enum {
  SEND_STATE_HEADER,
  SEND_STATE_BODY,
  SEND_STATE_DONE
} send_state_t;

typedef struct client {
  int fd;
  int epoll_fd;

  send_state_t send_state;

  char *header_data;
  size_t header_len;
  size_t header_sent;

  char *body_data;
  size_t body_len;
  size_t body_sent;

  int file_fd;
  char *file_data;
  size_t file_size;
  size_t file_sent;
  char file_path[256];

  char *request_buffer;
  size_t request_len;
  int request_complete;

  request_t *request;

  server_config *parent_server;

  int keep_alive;

  timer_node_t *timer_node;
} client_t;

struct timer_node {
  client_t *client;
  timer_node_t *prev;
  timer_node_t *next;
};

#define WHEEL_SIZE 60
#define TICK_INTERVAL_SECONDS 1
static timer_node_t *timer_wheel[WHEEL_SIZE];
static int current_tick = 0;

void timer_init() {
  for (int i = 0; i < WHEEL_SIZE; ++i) {
    timer_wheel[i] = NULL;
  }
}

void add_timer(client_t *client, int timeout_ms) {
  if (timeout_ms <= 0) {
    timeout_ms = 1;
  }
  int ticks_to_add = (timeout_ms / 1000) / TICK_INTERVAL_SECONDS;
  int slot = (current_tick + ticks_to_add) % WHEEL_SIZE;
  timer_node_t *node = malloc(sizeof(timer_node_t));
  if (!node) {
    perror("Failed to allocate timer node");
    return;
  }
  node->client = client;
  node->prev = NULL;
  node->next = timer_wheel[slot];
  if (timer_wheel[slot] != NULL) {
    timer_wheel[slot]->prev = node;
  }
  timer_wheel[slot] = node;
  client->timer_node = node;
}

void remove_timer(client_t *client) {
  timer_node_t *node = client->timer_node;
  if (!node)
    return;
  if (node->prev) {
    node->prev->next = node->next;
  } else {
    for (int i = 0; i < WHEEL_SIZE; ++i) {
      if (timer_wheel[i] == node) {
        timer_wheel[i] = node->next;
        break;
      }
    }
  }
  if (node->next) {
    node->next->prev = node->prev;
  }
  free(node);
  client->timer_node = NULL;
}

void tick_timer_wheel() {
  current_tick = (current_tick + 1) % WHEEL_SIZE;
  timer_node_t *current = timer_wheel[current_tick];
  timer_wheel[current_tick] = NULL;
  while (current != NULL) {
    timer_node_t *next = current->next;
    client_t *client = current->client;

    remove_timer(client);
    close_connection(client);
    current = next;
  }
}

client_t *initialise_client() {
  client_t *client = malloc(sizeof(client_t));
  if (!client) {
    return NULL;
  }
  memset(client, 0, sizeof(client_t));

  client->fd = -1;
  client->epoll_fd = -1;

  client->send_state = SEND_STATE_HEADER;

  client->header_data =
      (char *)malloc(global_config->http->headers_buffer_size);
  client->header_len = 0;
  client->header_sent = 0;

  /*
client->body_data = (char*)malloc(global_config->http->body_buffer_size);
client->body_len = 0;
client->body_sent = 0;
  */

  client->file_fd = -1;
  client->file_data = (char *)malloc(global_config->http->body_buffer_size);
  client->file_size = 0;
  client->file_sent = 0;

  client->request_buffer =
      (char *)malloc(global_config->http->default_buffer_size);
  client->request_len = 0;
  client->request_complete = 0;

  client->request = (request_t *)malloc(sizeof(request_t));
  if (!client->request) {
    perror("Failed to allocate memory for request");
    free(client);
    return NULL;
  }
  memset(client->request, 0, sizeof(request_t));
  client->request->body_data =
      (char *)malloc(global_config->http->default_buffer_size);
  client->request->headers = create_hashmap();
  if (!client->request->headers) {
    free(client->request);
    client->request = NULL;
    free(client);
    return NULL;
  }

  client->parent_server = NULL;

  client->timer_node = NULL;

  return client;
}

void write_pid_to_file(int value) {
  char filename[256];

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", t);

  snprintf(filename, sizeof(filename), "%d.txt", getpid());

  FILE *file = fopen(filename, "w");
  if (file == NULL) {
    perror("fopen");
    return;
  }

  fprintf(file, "%s Total connections left: %d\nRequest count: %lld\n",
          timestamp, value, request_count);

  fclose(file);
}

void free_request(request_t *request) {
  if (request) {
    if (request->headers)
      free_hashmap(request->headers);

    if (request->body_data)
      free(request->body_data);

    free(request);
  }
}

void free_client(client_t *client) {
  if (client) {
    if (client->header_data)
      free(client->header_data);

    if (client->file_fd != -1)
      close(client->file_fd);
    if (client->file_data)
      free(client->file_data);

    if (client->request_buffer)
      free(client->request_buffer);

    if (client->timer_node)
      remove_timer(client);

    free_request(client->request);

    free(client);
  }
}

void close_connection(client_t *client) {
  if (client == NULL) {
    return;
  }

  if (epoll_ctl(client->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL) == -1) {
    if (errno != EBADF) {
      perror("epoll_ctl: EPOLL_CTL_DEL");
    }
  }

  close(client->fd);

  free_client(client);

  my_connections--;
  atomic_fetch_sub(total_connections, 1);
}

static char *str_trim(char *str) {
  if (!str)
    return NULL;
  while (*str &&
         (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')) {
    str++;
  }
  char *end = str + strlen(str) - 1;
  while (end > str &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    *end = '\0';
    end--;
  }
  return str;
}

int parse_request(client_t *client) {
  if (!client->request_complete || client->request_len == 0) {
    return -1;
  }

  char *request_copy = strdup(client->request_buffer);
  if (!request_copy) {
    perror("Failed to duplicate request buffer");
    return -1;
  }

  char *saveptr_line, *saveptr_token;
  char *line = strtok_r(request_copy, "\r\n", &saveptr_line);

  if (line) {
    char *method = strtok_r(line, " ", &saveptr_token);
    if (method) {
      strncpy(client->request->method, method,
              sizeof(client->request->method) - 1);
      client->request->method[sizeof(client->request->method) - 1] = '\0';
    }

    char *uri = strtok_r(NULL, " ", &saveptr_token);
    if (uri) {
      strncpy(client->request->uri, uri, sizeof(client->request->uri) - 1);
      client->request->uri[sizeof(client->request->uri) - 1] = '\0';
    }

    char *version = strtok_r(NULL, " ", &saveptr_token);
    if (version) {
      strncpy(client->request->http_version, version,
              sizeof(client->request->http_version) - 1);
      client->request->http_version[sizeof(client->request->http_version) - 1] =
          '\0';
    }
  }

  while ((line = strtok_r(NULL, "\r\n", &saveptr_line)) != NULL) {
    if (strlen(line) == 0) {
      break;
    }

    char *colon = strchr(line, ':');
    if (colon) {
      *colon = '\0';
      char *key = str_trim(line);
      char *value = str_trim(colon + 1);
      if (key && value) {
        if (insert_hashmap(client->request->headers, key, value) != 0) {
          free(request_copy);
          return -1;
        }
      }
    }
  }

  char *body_start = strstr(client->request_buffer, "\r\n\r\n");
  if (body_start) {
    body_start += 4;
    size_t body_len =
        client->request_len - (body_start - client->request_buffer);
    if (body_len > 0 && body_len < global_config->http->default_buffer_size) {
      memcpy(client->request->body_data, body_start, body_len);
      client->request->body_data[body_len] = '\0';
      client->request->body_len = body_len;
    }
  }

  free(request_copy);
  return 0;
}

int send_headers(client_t *client) {
  while (client->header_sent < client->header_len) {
    const char *to_write = client->header_data + client->header_sent;
    int to_write_len = client->header_len - client->header_sent;
    ssize_t bytes_written = write(client->fd, to_write, to_write_len);

    if (bytes_written > 0) {
      client->header_sent += bytes_written;
      if (client->header_sent >= client->header_len) {
        client->send_state = SEND_STATE_BODY;
      }
    } else if (bytes_written == -1 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return 1;
    } else {
      perror("write header");
      close_connection(client);
      return -1;
    }
  }

  return 0;
}

int send_body(client_t *client) {
  while (client->body_sent < client->body_len) {
    const char *to_write = client->body_data + client->body_sent;
    int to_write_len = client->body_len - client->body_sent;
    ssize_t bytes_written = write(client->fd, to_write, to_write_len);

    if (bytes_written > 0) {
      client->body_sent += bytes_written;
      if (client->body_sent >= client->body_len) {
        client->send_state = SEND_STATE_DONE;
      }
    } else if (bytes_written == -1 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return 1;
    } else {
      perror("write body");
      close_connection(client);
      return -1;
    }
  }

  return 0;
}

int is_directory(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

int find_file(client_t *client, char *uri) {
  char *search_uri = client->request->uri;
  if (uri)
    search_uri = uri;

  server_config *server = client->parent_server;
  route_config *matched_route = NULL;

  char *content_dir = server->content_dir;
  char **index_files = server->index_files;

  for (int i = 0; i < server->num_routes; i++) {
    if (strcmp(server->routes[i].uri, search_uri) == 0) {
      matched_route = &server->routes[i];
      break;
    }
  }

  if (matched_route && matched_route->content_dir) {
    content_dir = matched_route->content_dir;
  }

  if (matched_route && matched_route->num_index_files > 0) {
    index_files = matched_route->index_files;
  }

  char *full_path = NULL;
  if (asprintf(&full_path, "%s%s", content_dir, search_uri) == -1) {
    return -1;
  }

  char *resolved = realpath(full_path, NULL);
  free(full_path);

  if (!resolved || is_directory(resolved)) {
    free(resolved);

    if (search_uri[strlen(search_uri) - 1] == '/') {
      for (int i = 0; i < server->num_index_files; i++) {
        if (asprintf(&full_path, "%s%s%s", content_dir, search_uri,
                     index_files[i]) == -1) {
          continue;
        }
        resolved = realpath(full_path, NULL);
        free(full_path);
        if (resolved)
          break;
      }
    } else {
      const char *fallbacks[] = {".html", ".htm", ".txt"};
      for (int i = 0; i < 3; i++) {
        if (asprintf(&full_path, "%s%s%s", content_dir, search_uri,
                     fallbacks[i]) == -1) {
          continue;
        }
        resolved = realpath(full_path, NULL);
        free(full_path);
        if (resolved)
          break;
      }
    }
  }

  if (!resolved) {
    return -1;
  }

  client->file_fd = open(resolved, O_RDONLY);
  if (client->file_fd == -1) {
    perror("open");
    free(resolved);
    return -1;
  }

  struct stat st;
  if (fstat(client->file_fd, &st) == -1) {
    perror("fstat");
    close(client->file_fd);
    client->file_fd = -1;
    free(resolved);
    return -1;
  }

  client->file_size = st.st_size;
  client->file_sent = 0;
  strncpy(client->file_path, resolved, sizeof(client->file_path) - 1);
  client->file_path[sizeof(client->file_path) - 1] = '\0';

  free(resolved);
  return 0;
}

int send_file_with_write(client_t *client) {
  while (client->file_sent < client->file_size) {
    size_t bytes_to_read = client->file_size - client->file_sent;
    if (bytes_to_read > global_config->http->body_buffer_size) {
      bytes_to_read = global_config->http->body_buffer_size;
    }
    ssize_t bytes_read = pread(client->file_fd, client->file_data,
                               bytes_to_read, client->file_sent);

    if (bytes_read == -1) {
      perror("pread");
      return -1;
    }
    if (bytes_read == 0) {
      break;
    }

    size_t bytes_left_to_write = bytes_read;
    char *write_ptr = client->file_data;

    while (bytes_left_to_write > 0) {
      ssize_t bytes_written = write(client->fd, write_ptr, bytes_left_to_write);

      if (bytes_written == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return 1;
        } else if (errno == EPIPE) {
          return -1;
        } else {
          perror("write");
          return -1;
        }
      } else if (bytes_written > 0) {
        bytes_left_to_write -= bytes_written;
        write_ptr += bytes_written;

        client->file_sent += bytes_written;
      }
    }
  }

  client->send_state = SEND_STATE_DONE;
  return 0;
}

int send_file_with_sendfile(client_t *client) {
  off_t offset = client->file_sent;

  while (client->file_sent < client->file_size) {
    ssize_t bytes_sent = sendfile(client->fd, client->file_fd, &offset,
                                  client->file_size - client->file_sent);

    if (bytes_sent == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;
      } else if (errno == EPIPE) {
        return -1;
      } else {
        perror("sendfile");
        return -1;
      }
    }

    if (bytes_sent == 0) {
      break;
    }

    client->file_sent += bytes_sent;
  }

  if (client->file_sent >= client->file_size) {
    client->send_state = SEND_STATE_DONE;
  }

  return 0;
}

int build_headers(client_t *client, int status_code, long long content_length,
                  char *connection, const char *mime_type) {
  char header_template[] = "HTTP/1.1 %d %s\r\n"
                           "Content-Length: %lld\r\n"
                           "Connection: %s\r\n"
                           "Content-Type: %s\r\n"
                           "\r\n";

  char *status_message = get_status_message(status_code);

  char header_buf[256];
  int header_len =
      snprintf(header_buf, sizeof(header_buf), header_template, status_code,
               status_message, content_length, connection, mime_type);

  memcpy(client->header_data, header_buf, header_len + 1);
  client->header_len = header_len;
  client->header_sent = 0;
  client->send_state = SEND_STATE_HEADER;

  return 0;
}

void reset_client(client_t *client) {
  // reset request data
  memset(client->request->method, 0, sizeof(client->request->method));
  memset(client->request->uri, 0, sizeof(client->request->uri));
  memset(client->request->http_version, 0,
         sizeof(client->request->http_version));
  free_hashmap(client->request->headers);
  client->request->headers = create_hashmap();
  if (!client->request->headers) {
    close_connection(client);
    return;
  }
  memset(client->request->body_data, 0,
         global_config->http->default_buffer_size);
  client->request->body_len = 0;

  // reset client data
  client->send_state = SEND_STATE_HEADER;

  memset(client->header_data, 0, global_config->http->headers_buffer_size);
  client->header_len = 0;
  client->header_sent = 0;

  /*
  memset(client->body_data, 0, global_config->http->body_buffer_size);
client->body_len = 0;
client->body_sent = 0;
  */

  if (client->file_fd != -1)
    close(client->file_fd);
  client->file_fd = -1;
  memset(client->file_data, 0, global_config->http->body_buffer_size);
  client->file_size = 0;
  client->file_sent = 0;
  memset(client->file_path, 0, sizeof(client->file_path));

  memset(client->request_buffer, 0, global_config->http->default_buffer_size);
  client->request_len = 0;
  client->request_complete = 0;
}

int setup_epoll(int *listen_sockets) {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  struct epoll_event event;
  int num_sockets = global_config->http->num_servers;

  for (int i = 0; i < num_sockets; i++) {
    event.events = EPOLLIN | EPOLLEXCLUSIVE;
    event.data.fd = listen_sockets[i];

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sockets[i], &event) == -1) {
      close(epoll_fd);
      perror("epoll_ctl: listen_sockets[i]");
      exit(EXIT_FAILURE);
    }
  }
  return epoll_fd;
}

void worker_loop(int *listen_sockets) {
  signal(SIGINT, SIG_IGN);
  struct sigaction sa_term;
  memset(&sa_term, 0, sizeof(sa_term));
  sa_term.sa_handler = worker_signal_handler;
  sa_term.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &sa_term, NULL);

  int new_conn_fd;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len;
  struct epoll_event event, events[MAX_EVENTS];
  int epoll_fd = setup_epoll(listen_sockets);

  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  if (timer_fd == -1) {
    perror("timerfd_create");
    exit(EXIT_FAILURE);
  }
  struct itimerspec new_value;
  new_value.it_value.tv_sec = TICK_INTERVAL_SECONDS;
  new_value.it_value.tv_nsec = 0;
  new_value.it_interval.tv_sec = TICK_INTERVAL_SECONDS;
  new_value.it_interval.tv_nsec = 0;
  if (timerfd_settime(timer_fd, 0, &new_value, NULL) == -1) {
    perror("timerfd_settime");
    close(timer_fd);
    exit(EXIT_FAILURE);
  }
  event.events = EPOLLIN;
  event.data.fd = timer_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &event) == -1) {
    perror("epoll_ctl: timerfd");
    close(timer_fd);
    exit(EXIT_FAILURE);
  }

  printf("Worker %d is running and waiting for connections...\n", getpid());

  while (worker_running) {
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
    if (num_events == -1) {
      if (errno == EINTR)
        if (!worker_running)
          break;
      continue;
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < num_events; ++i) {
      int current_fd = events[i].data.fd;

      if (current_fd == timer_fd) {
        uint64_t ticks;
        if (read(timer_fd, &ticks, sizeof(ticks)) == sizeof(ticks)) {
          tick_timer_wheel();
        }
        continue;
      }

      int is_listening_socket = 0;
      for (int j = 0; j < global_config->http->num_servers; j++) {
        if (current_fd == listen_sockets[j]) {
          is_listening_socket = 1;
          break;
        }
      }

      if (is_listening_socket) {
        client_addr_len = sizeof(client_addr);
        while ((new_conn_fd = accept4(
                    current_fd, (struct sockaddr *)&client_addr,
                    &client_addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC)) != -1) {
          if (atomic_load(total_connections) >=
              global_config->max_connections) {
            printf("Max connections reached\n");
            close(new_conn_fd);
            continue;
          }

          if (new_conn_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            } else {
              perror("accept");
              break;
            }
          }

          set_nonblocking(new_conn_fd);

          int flag = 1;
          if (setsockopt(new_conn_fd, IPPROTO_TCP, TCP_NODELAY, &flag,
                         sizeof(flag)) == -1) {
            perror("setsockopt TCP_NODELAY");
          }

          my_connections++;
          atomic_fetch_add(total_connections, 1);

          request_count++;

          client_t *client = initialise_client();
          if (!client) {
            close(new_conn_fd);
            continue;
          }

          client->fd = new_conn_fd;
          client->epoll_fd = epoll_fd;

          for (int i = 0; i < global_config->http->num_servers; i++) {
            if (current_fd == listen_sockets[i]) {
              client->parent_server = &global_config->http->servers[i];
              break;
            }
          }

          event.events = EPOLLIN | EPOLLET;
          event.data.ptr = client;
          if (epoll_ctl(client->epoll_fd, EPOLL_CTL_ADD, client->fd, &event) ==
              -1) {
            close_connection(client);
            printf("closing connection1\n");
          }
          add_timer(client, client->parent_server->timeout);
        }

        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept");
        }
      } else {
        client_t *client = (client_t *)events[i].data.ptr;
        if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
          close_connection(client);
          continue;
        }

        if (events[i].events & EPOLLIN) {
          remove_timer(client);
          if (client->request_complete) {
            continue;
          }

          ssize_t bytes_read;
          while ((bytes_read = read(
                      client->fd, client->request_buffer + client->request_len,
                      global_config->http->default_buffer_size - 1 -
                          client->request_len)) > 0) {
            client->request_len += bytes_read;
            client->request_buffer[client->request_len] = '\0';
            if (strstr(client->request_buffer, "\r\n\r\n") != NULL) {
              client->request_complete = 1;
              break;
            }
            if (client->request_len >=
                global_config->http->default_buffer_size - 1) {
              close_connection(client);
              break;
            }
          }
          if (bytes_read == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            perror("read");
            close_connection(client);
            continue;
          } else if (bytes_read == 0 && client->request_len == 0) {
            close_connection(client);
            continue;
          }

          if (client->request_complete) {
            int status_code;
            long long content_length;
            char *connection;
            char *mime_type;

            int parse_request_status = parse_request(client);
            if (parse_request_status == -1) {
              status_code = 400;
            } else {
              status_code = 200;
            }

            if (parse_request_status == 0) {
              int find_file_status = find_file(client, NULL);
              if (find_file_status == -1) {
                status_code = 404;
                // TODO: add error file path in config
                find_file_status = find_file(client, "/404.html");
                if (find_file_status == -1) {
                  close_connection(client);
                  continue;
                }
              }
            }

            // set headers values
            content_length = client->file_size;
            connection =
                (char *)get_hashmap(client->request->headers, "Connection");
            if (connection != NULL && (strcmp(connection, "keep-alive") == 0 ||
                                       strcmp(connection, "Keep-Alive") == 0)) {
              client->keep_alive = 1;
            } else if (connection == NULL) {
              connection = "close";
              client->keep_alive = 0;
            }
            mime_type = get_mime_type(client->file_path);

            int build_headers_status = build_headers(
                client, status_code, content_length, connection, mime_type);
            if (build_headers_status == -1) {
              close_connection(client);
              continue;
            }

            event.events = EPOLLOUT | EPOLLET;
            event.data.ptr = client;
            if (epoll_ctl(client->epoll_fd, EPOLL_CTL_MOD, client->fd,
                          &event) == -1) {
              perror("epoll_ctl: mod client");
              close_connection(client);
            }
          }
        }

        if (events[i].events & EPOLLOUT) {
          int send_status;

          if (client->send_state == SEND_STATE_HEADER) {
            send_status = send_headers(client);
            if (send_status < 0) {
              close_connection(client);
              continue;
            }
          }

          if (client->send_state == SEND_STATE_BODY) {
            if (global_config->http->sendfile == 1) {
              send_status = send_file_with_sendfile(client);
            } else {
              send_status = send_file_with_write(client);
            }
            if (send_status < 0) {
              close_connection(client);
              continue;
            }
          }

          if (client->send_state == SEND_STATE_DONE) {
            if (client->keep_alive == 1) {
              reset_client(client);
              add_timer(client, client->parent_server->timeout);

              event.events = EPOLLIN | EPOLLET;
              event.data.ptr = client;
              if (epoll_ctl(client->epoll_fd, EPOLL_CTL_MOD, client->fd,
                            &event) == -1) {
                perror("epoll_ctl: mod client");
                close_connection(client);
                printf("closing connection8\n");
              }
            } else {
              close_connection(client);
            }
          }
        }
      }
    }
  }

  printf("Worker %d is exiting.\n", getpid());
  close(epoll_fd);
  close(timer_fd);
  free_mime_types();
  free_config();
  exit(EXIT_SUCCESS);
}

void init_sockets(int *listen_sockets) {
  server_config *servers = global_config->http->servers;

  for (int i = 0; i < global_config->http->num_servers; i++) {
    listen_sockets[i] = setup_listening_socket(servers[i].listen_port);

    if (listen_sockets[i] == -1) {
      exit(EXIT_FAILURE);
    }
  }
}

void setup_signals() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_signal;

  // handle Ctrl+C
  sigaction(SIGINT, &sa, NULL);

  // handle kill / server kill command
  sigaction(SIGTERM, &sa, NULL);

  // ignore broken pipe signals
  signal(SIGPIPE, SIG_IGN);
}

void start(int *listen_sockets) {
  if (global_config->pid_file) {
    FILE *pidf = fopen(global_config->pid_file, "w");
    if (!pidf) {
      perror("Failed to open pid_file");
      exit(EXIT_FAILURE);
    }
    fprintf(pidf, "%d\n", getpid());
    fclose(pidf);
  }

  for (int i = 0; i < global_config->http->num_servers; i++) {
    printf("Master process %d is listening on port %d...\n", getpid(),
           global_config->http->servers[i].listen_port);
  }

  pid_t worker_pids[global_config->worker_processes];
  for (int i = 0; i < global_config->worker_processes; ++i) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (pid == 0) {
      worker_loop(listen_sockets);
      exit(EXIT_SUCCESS);
    }
    worker_pids[i] = pid;
  }

  while (g_running) {
    sleep(1);
  }

  printf("Master process %d received termination signal. Shutting down "
         "workers...\n",
         getpid());

  for (int i = 0; i < global_config->worker_processes; ++i) {
    kill(worker_pids[i], SIGTERM);
  }

  int status;
  pid_t child_pid;
  for (int i = 0; i < global_config->worker_processes; ++i) {
    if ((child_pid = waitpid(worker_pids[i], &status, 0)) > 0) {
      printf("Worker process %d finished.\n", child_pid);
    }
  }

  printf("Total connections left: %d\n", atomic_load(total_connections));
  for (int i = 0; i < global_config->http->num_servers; i++) {
    close(listen_sockets[i]);
  }

  if (global_config->pid_file) {
    unlink(global_config->pid_file);
  }

  free_mime_types();
  free_config();
}

void start_server() {
  setup_signals();
  setup_total_connections();

  load_mime_types(global_config->http->mime_types_path);

  int listen_sockets[global_config->http->num_servers];
  init_sockets(listen_sockets);

  start(listen_sockets);
}

int kill_server() {
  FILE *f = fopen(global_config->pid_file, "r");
  if (!f)
    return -1;
  int pid;
  fscanf(f, "%d", &pid);
  fclose(f);

  if (kill(pid, SIGTERM) == -1) {
    perror("Failed to kill server");
    return -1;
  }
  return 0;
}

void daemonise() {
  pid_t pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE); // fork failed
  if (pid > 0)
    exit(EXIT_SUCCESS); // parent exits

  if (setsid() < 0)
    exit(EXIT_FAILURE); // new session
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);

  umask(0);

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

int is_server_running() {
  if (!global_config->pid_file)
    return 0;

  FILE *f = fopen(global_config->pid_file, "r");
  if (!f)
    return 0;

  int pid;
  if (fscanf(f, "%d", &pid) != 1) {
    fclose(f);
    return 0;
  }
  fclose(f);

  if (kill(pid, 0) == 0) {
    return 1;
  } else {
    if (errno == ESRCH)
      return 0;
    return 1;
  }
}

int get_total_connections() {
	const char *shm_name = "/server_connections";

  int shm_fd;
  atomic_int *total_connections_shm;
  int connection_count = -1;

  // open the shared memory object in read-only mode
  shm_fd = shm_open(shm_name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    if (errno == ENOENT) {
      return 0;
    }
    perror("ERROR: shm_open failed");
    return -1;
  }

  total_connections_shm =
      mmap(NULL, sizeof(atomic_int), PROT_READ, MAP_SHARED, shm_fd, 0);
  if (total_connections_shm == MAP_FAILED) {
    perror("ERROR: mmap failed");
    close(shm_fd);
    return -1;
  }

  connection_count = atomic_load(total_connections_shm);

  munmap(total_connections_shm, sizeof(atomic_int));
  close(shm_fd);

  return connection_count;
}
void display_status() {
  FILE *f;
  int pid;

  int status = is_server_running();
  if (status == 0) {
    printf("Server is not running.\n");
    return;
  } else if (status == -1) {
    printf("Could not determine server status due to an error.\n");
    return;
  }

  f = fopen(global_config->pid_file, "r");
  if (!f) {
    // this case should not be hit if is_server_running() returned 1
    perror("Failed to open PID file after server was confirmed running");
    return;
  }
  fscanf(f, "%d", &pid);
  fclose(f);

  printf("Server Status:\n");
  printf("  PID: %d\n", pid);

  // get the uptime by reading from the /proc filesystem
  char proc_path[256];
  snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);

  f = fopen(proc_path, "r");
  if (f) {
    long start_time_ticks;
    long system_uptime_ticks;
    long clock_ticks_per_sec = sysconf(_SC_CLK_TCK);

    // read the 22nd field (start time) from the stat file
    // the * in %*d tells scanf to read and discard the value
    // reading all the fields is more robust than just seeking
    if (fscanf(f,
               "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u "
               "%*d %*d %*d %*d %*d %*d %ld",
               &start_time_ticks) == 1) {
      fclose(f);

      f = fopen("/proc/uptime", "r");
      if (f) {
        if (fscanf(f, "%ld", &system_uptime_ticks) == 1) {
          fclose(f);

          long uptime_seconds =
              (long)(system_uptime_ticks -
                     (start_time_ticks / (double)clock_ticks_per_sec));
          long hours = uptime_seconds / 3600;
          long minutes = (uptime_seconds % 3600) / 60;
          long seconds = uptime_seconds % 60;

          printf("  Uptime: %ldh %ldm %lds\n", hours, minutes, seconds);
        } else {
          fclose(f);
          printf("  Error: Failed to read system uptime.\n");
        }
      } else {
        printf("  Error: Failed to open /proc/uptime.\n");
      }
    } else {
      fclose(f);
      printf("  Error: Failed to parse process stat file.\n");
    }
  } else {
    printf("  Error: Failed to open /proc/%d/stat. Check permissions or if the "
           "process has terminated.\n",
           pid);
  }

  printf("  Total Connections: %d\n", get_total_connections());

  if (global_config) {
    printf("  Config File: %s\n", global_config->pid_file);
    printf("  Workers: %d\n", global_config->worker_processes);
  }
}

void print_usage() {
  printf("Usage: %s [run | kill | restart] [OPTIONS]\n", NAME);
  printf("\nOptions:\n");
  printf("  -c <file>, --config <file>   Specify config file (default: "
         "/etc/%s/%s.conf)\n",
         NAME, NAME);
  printf("  -h, --help                   Show this help message\n");
  printf("  -v, --version                Show version\n");
  printf("  -f, --foreground             Run the server in the foreground\n");
  printf("  -s, --status                 Check if the server is running\n");
}

int main(int argc, char *argv[]) {
  char *config_path = DEFAULT_CONFIG_PATH;
  int foreground = 0;
  char *command = NULL;

  if (argc < 2) {
    print_usage();
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
      if (i + 1 < argc) {
        config_path = argv[i + 1];
        i++;
      }
    } else if (strcmp(argv[i], "-f") == 0 ||
               strcmp(argv[i], "--foreground") == 0) {
      foreground = 1;
    } else {
      if (command == NULL) {
        command = argv[i];
      }
    }
  }

  load_config(config_path);
	check_config();

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    } else if (strcmp(argv[i], "-v") == 0 ||
               strcmp(argv[i], "--version") == 0) {
      printf("%s version %s\n", NAME, VERSION);
      return 0;
    } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
      if (is_server_running() == 1) {
        display_status();
      } else {
        printf("Server is not running.\n");
      }
      return 0;
    }
  }

  if (command == NULL) {
    print_usage();
    return 1;
  }

  pid_t pid = getpid();

  if (strcmp(command, "run") == 0) {
    if (is_server_running()) {
      fprintf(stderr, "Server is already running.\n");
      return 1;
    }
    printf("Starting server with PID %d...\n", pid);
    printf("Run '%s kill' to kill the server.\n", argv[0]);
    if (!foreground) {
      daemonise();
    }
    start_server();
  } else if (strcmp(command, "kill") == 0) {
    if (!is_server_running()) {
      fprintf(stderr, "Server is not running.\n");
      return 1;
    }
    printf("Killing server...\n");
    kill_server();
  } else if (strcmp(command, "restart") == 0) {
    if (is_server_running()) {
      printf("Restarting server...\n");
      kill_server();
      sleep(1);
    }
    printf("New server instance is running.\n");
    if (!foreground) {
      daemonise();
    }
    start_server();
  } else {
    fprintf(stderr, "Unknown command '%s'\n", command);
    print_usage();
    return 1;
  }

  return 0;
}
