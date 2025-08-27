#include <arpa/inet.h>
#include <fcntl.h>
#include <glib.h>
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

#include "util.h"

int logs_enabled = 1;
int verbose_mode_enabled = 1;

void logs(char type, const char *fmt, const char *extra_fmt, ...) {
  if (!logs_enabled) {
    return;
  }

  char log_filename[256];
  snprintf(log_filename, sizeof(log_filename), "logs.log");

  FILE *log_file = fopen(log_filename, "a");
  if (!log_file) {
    log_file = stderr;
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", t);

  va_list args;
  va_start(args, extra_fmt);

  va_list args_copy;
  va_copy(args_copy, args);

  // Print timestamp and prefix to the selected file stream
  fprintf(log_file, "%s", timestamp); // Print the timestamp first

  switch (type) {
  case 'E':
    fprintf(log_file, "[ERROR] ");
    break;
  case 'W':
    fprintf(log_file, "[WARN] ");
    break;
  case 'I':
    fprintf(log_file, "[INFO] ");
    break;
  case 'D':
    fprintf(log_file, "[DEBUG] ");
    break;
  default:
    fprintf(log_file, "[ERROR] Invalid log type: %c\n", type);
    va_end(args);
    va_end(args_copy);
    if (log_file != stderr)
      fclose(log_file);
    return;
  }

  vfprintf(log_file, fmt, args);
  fprintf(log_file, "\n");

  if (extra_fmt && verbose_mode_enabled) {
    fprintf(log_file, "EXTRA: ");
    vfprintf(log_file, extra_fmt, args_copy);
    fprintf(log_file, "\n");
  }

  va_end(args);
  va_end(args_copy);

  if (log_file != stderr) {
    fclose(log_file);
  }
}

/*
void logs(char type, const char *fmt, const char *extra_fmt, ...) {
	if (!logs_enabled) {
		return;
	}

  va_list args;
  va_start(args, extra_fmt);

  // copy the variadic arguments so we can use them twice
  va_list args_copy;
  va_copy(args_copy, args);

  // print prefix
  switch (type) {
  case 'E':
    fprintf(stderr, "ERROR: ");
    break;
  case 'W':
    fprintf(stderr, "WARNING: ");
    break;
  case 'I':
    fprintf(stderr, "INFO: ");
    break;
  case 'D':
    fprintf(stderr, "DEBUG: ");
    break;
  default:
    fprintf(stderr, "ERROR: Invalid log type: %c\n", type);
    va_end(args);
    va_end(args_copy);
    return;
  }

  // print main message
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  // print extra if in verbose mode
  if (extra_fmt && verbose_mode_enabled) {
    fprintf(stderr, "EXTRA: ");
    vfprintf(stderr, extra_fmt, args_copy);
    fprintf(stderr, "\n");
  }

  va_end(args);
  va_end(args_copy);
}
*/

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
