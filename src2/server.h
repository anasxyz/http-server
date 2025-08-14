#ifndef _SERVER_H_
#define _SERVER_H_

#include "config.h"
#include "util.h"

/*
// --- connection handling ---
int handle_new_connection(int connection_socket, int epoll_fd);
void close_connection(client_t *client, int epoll_fd);

// --- request handling ---
int read_client_request(client_t *client);

// --- response handling ---
void write_response(int sock, const char *response_msg); // function for writing normal full response to client
void write_client_response(client_t *client, int epoll_fd);
void serve_file(client_t *client, int epoll_fd); // function for serving a file to client

// --- server process handling ---
void run_worker(int *listen_sockets, int num_sockets);
void init_sockets(int *listen_sockets);
void fork_workers(int *listen_sockets);
void server_cleanup(int *listen_sockets);

// --- config handling ---
void check_valid_config();
*/

#endif // _SERVER_H_
