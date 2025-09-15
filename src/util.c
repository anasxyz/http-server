#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

int logs_enabled = 0;
int verbose_mode_enabled = 0;

/*
void logs(char type, const char *fmt, const char *extra_fmt, ...) {
  if (!logs_enabled) {
    return;
  }

  char log_filename[256];
  snprintf(log_filename, sizeof(log_filename), "worker_%d.log", getpid());

  FILE *log_file = fopen(log_filename, "a");
  if (!log_file) {
    log_file = stderr;
  }

  va_list args;
  va_start(args, extra_fmt);

  va_list args_copy;
  va_copy(args_copy, args);

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
*/

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

  fprintf(log_file, "%s", timestamp);

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

  va_list args_copy;
  va_copy(args_copy, args);

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

  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

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
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}

char *get_status_message(int code) {
  switch (code) {
  case 100:
    return "Continue";
  case 101:
    return "Switching Protocols";
  case 102:
    return "Processing";
  case 103:
    return "Early Hints";

  case 200:
    return "OK";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 203:
    return "Non-Authoritative Information";
  case 204:
    return "No Content";
  case 205:
    return "Reset Content";
  case 206:
    return "Partial Content";
  case 207:
    return "Multi-Status";
  case 208:
    return "Already Reported";
  case 226:
    return "IM Used";

  case 300:
    return "Multiple Choices";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Found";
  case 303:
    return "See Other";
  case 304:
    return "Not Modified";
  case 305:
    return "Use Proxy";
  case 307:
    return "Temporary Redirect";
  case 308:
    return "Permanent Redirect";

  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 402:
    return "Payment Required";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 406:
    return "Not Acceptable";
  case 407:
    return "Proxy Authentication Required";
  case 408:
    return "Request Timeout";
  case 409:
    return "Conflict";
  case 410:
    return "Gone";
  case 411:
    return "Length Required";
  case 412:
    return "Precondition Failed";
  case 413:
    return "Payload Too Large";
  case 414:
    return "URI Too Long";
  case 415:
    return "Unsupported Media Type";
  case 416:
    return "Range Not Satisfiable";
  case 417:
    return "Expectation Failed";
  case 418:
    return "I'm a teapot";
  case 421:
    return "Misdirected Request";
  case 422:
    return "Unprocessable Entity";
  case 423:
    return "Locked";
  case 424:
    return "Failed Dependency";
  case 425:
    return "Too Early";
  case 426:
    return "Upgrade Required";
  case 428:
    return "Precondition Required";
  case 429:
    return "Too Many Requests";
  case 431:
    return "Request Header Fields Too Large";
  case 451:
    return "Unavailable For Legal Reasons";

  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 502:
    return "Bad Gateway";
  case 503:
    return "Service Unavailable";
  case 504:
    return "Gateway Timeout";
  case 505:
    return "HTTP Version Not Supported";
  case 506:
    return "Variant Also Negotiates";
  case 507:
    return "Insufficient Storage";
  case 508:
    return "Loop Detected";
  case 510:
    return "Not Extended";
  case 511:
    return "Network Authentication Required";

  default:
    return "Unknown Status";
  }
}

int is_empty(char *str) {
	return str == NULL || *str == '\0' || *str == '\n' || *str == '\r' || strcmp(str, "");
}
