#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "http.h"

const char *get_mime_type(const char *path) {
  const char *extension = strrchr(path, '.');

  if (extension == NULL) {
    return "application/octet-stream";
  }

  if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) { return "text/html"; }
  if (strcmp(extension, ".css") == 0) { return "text/css"; }
  if (strcmp(extension, ".js") == 0) { return "text/javascript"; }
  if (strcmp(extension, ".json") == 0) { return "application/json"; }
  if (strcmp(extension, ".png") == 0) { return "image/png"; }
  if (strcmp(extension, ".jpg") == 0) { return "image/jpeg"; }
  if (strcmp(extension, ".gif") == 0) { return "image/gif"; }
  if (strcmp(extension, ".txt") == 0) { return "text/plain"; }

  return "application/octet-stream"; // default MIME type for unknown extensions
}

// reads and serves requested file
void serve_file(int socket, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    // 404 not found
    char *not_found_body = "<html><body><h1>404 Not Found</h1></body></html>";
    // not using get_mime_type() here just because we don't know the file extension
    send_response(socket, "404 not found", "text/html", not_found_body); 
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
  const char *mime_type = get_mime_type(path);
  send_response(socket, "200 OK", mime_type, file_buffer);

  // free memory
  free(file_buffer);
}


