#ifndef _SERVER_H_
#define _SERVER_H_

#include "config.h"
#include "defaults.h"
#include "hashmap.h"
#include "mime.h"
#include "util.h"

#define MAX_EVENTS (2 * 1024)

typedef struct timer_node timer_node_t;
#define WHEEL_SIZE 60
#define TICK_INTERVAL_SECONDS 1

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

void handle_singal(int sig);
void worker_signal_handler(int sig);
void cleanup_shm();
void setup_total_connections();

void timer_init();
void add_timer(client_t *client, int timeout_ms);
void remove_timer(client_t *client);
void tick_timer_wheel();

client_t *initialise_client();
void free_request(request_t *request);
void free_client(client_t *client);

void close_connection(client_t *client);

int parse_request(client_t *client);
int send_headers(client_t *client);
int send_body(client_t *client);
int is_directory(const char *path);
int find_file(client_t *client, char *uri);
int send_file_with_write(client_t *client);
int send_file_with_sendfile(client_t *client);
int build_headers(client_t *client, int status_code, long long content_length,
                  char *connection, const char *mime_type);

int setup_epoll(int *listen_sockets);
void worker_loop(int *listen_sockets);

void init_sockets(int *listen_sockets);
void setup_signals();
void start(int *listen_sockets);
void start_server();

#endif // _SERVER_H_
