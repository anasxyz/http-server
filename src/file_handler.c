#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/http.h"
#include "../include/file_handler.h"
#include "../include/utils_path.h"

// serves 404 not found page
void serve_not_found(int socket) {
  FILE *file = fopen("www/404.html", "rb");
  if (!file) {
    perror("Failed to open 404.html...\n");

    const char* fallback_body = "<html><body><h1>404 Not Found</h1></body></html>";
    HttpResponse response = {
      .status = "404 Not Found",
      .content_type = "text/html",
      .body = (char *)fallback_body,  // no need to free this because it's a static string
      .body_length = strlen(fallback_body),
      .headers = NULL,
      .num_headers = 0,
    };

    send_response(socket, &response);
    return;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *file_buffer = malloc(file_size);
  if (!file_buffer) {
    fclose(file);
    return;
  }

  fread(file_buffer, 1, file_size, file);
  fclose(file);

  HttpResponse response = {
    .status = "404 Not Found",
    .content_type = get_mime_type("www/404.html"),
    .body = file_buffer,
    .body_length = file_size,
    .headers = NULL,
    .num_headers = 0,
  };

  send_response(socket, &response);

  free(file_buffer);
}

// reads and serves requested file
void serve_file(int socket, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    perror("Failed to open file...\n");

    serve_not_found(socket);
    return;
  }

  // get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // read file into buffer
  char *file_buffer = malloc(file_size);
  if (!file_buffer) {
    fclose(file);
    return;
  }

  fread(file_buffer, 1, file_size, file);
  fclose(file);

  // build response header
  char *mime_type = get_mime_type(path);

  HttpResponse response = {
    .status = "200 OK",
    .content_type = mime_type,
    .body = file_buffer,
    .body_length = file_size,
    .headers = NULL,
    .num_headers = 0,
  };

  send_response(socket, &response);

  // free memory
  free(file_buffer);
}

