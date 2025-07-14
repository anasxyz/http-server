#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/http.h"

#define WEB_ROOT "www"

char *get_mime_type(const char *path) {
  const char *extension = strrchr(path, '.');
  if (extension == NULL) { return "application/octet-stream"; }

  // TODO: add more MIME types
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
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

bool does_path_exist(const char *path) {
  if (!path) { return NULL; }

  if (realpath(path, NULL) != NULL) {
    return true;
  } else {
    return false;
  }
}

// this took ridiuculously long to figure out
const char* clean_path(const char* path) {
    if (!path) return NULL;

    size_t len = strlen(path);
    if (len == 0) return strdup("");

    // allocate room for components
    char** stack = malloc(sizeof(char*) * (len + 1));
    char* path_copy = strdup(path);
    char* token;
    size_t stack_size = 0;

    int is_absolute = (path[0] == '/');
    int has_trailing_slash = (path[len - 1] == '/');

    // tokenise and build component stack
    token = strtok(path_copy, "/");
    while (token != NULL) {
        if (strcmp(token, "..") == 0) {
            if (stack_size > 0 && strcmp(stack[stack_size - 1], "..") != 0) {
                stack_size--;
            } else if (!is_absolute) {
                // relative path so keep ".."
                stack[stack_size++] = token;
            }
        } else if (strcmp(token, ".") != 0 && strlen(token) > 0) {
            stack[stack_size++] = token;
        }
        token = strtok(NULL, "/");
    }

    // estimate total size
    size_t result_len = 1 + (len * 2);
    char* cleaned = malloc(result_len);
    if (!cleaned) return NULL;
    cleaned[0] = '\0';

    if (is_absolute) strcat(cleaned, "/");

    for (size_t i = 0; i < stack_size; ++i) {
        strcat(cleaned, stack[i]);
        if (i != stack_size - 1) strcat(cleaned, "/");
    }

    // re add trailing slash if needed
    if (has_trailing_slash && stack_size > 0) {
        strcat(cleaned, "/");
    }

    // if result is empty just return "." for relative or "/" for absolute
    if (strlen(cleaned) == 0) {
        strcpy(cleaned, is_absolute ? "/" : ".");
    }

    free(stack);
    free(path_copy);
    return cleaned;
}

// this is the old version of the clean_path() function
/*
char* clean_path(const char *request_path) {
  if (!request_path) { return NULL; }

  char temp_path[512];
  if (strcmp(request_path, "/") == 0) {
    // if asked for root, serve index.html
    snprintf(temp_path, sizeof(temp_path), "%s/index.html", WEB_ROOT);
  } else {
    // otherwise just serve whatever the path is
    // also get rid of duplicate slashes
    const char *path_start = request_path;
    if (request_path[0] != '/') { path_start++; } // skip leading '/' if there is one
    snprintf(temp_path, sizeof(temp_path), "%s/%s", WEB_ROOT, path_start);
  }

  // https://pubs.opengroup.org/onlinepubs/009696799/functions/realpath.html
  // use realpath to get absolute canonical path without symbolic links or ".." or "."
  // also checks if path exists
  // must be freed later because it allocates memory
  char *cleaned_path = realpath(temp_path, NULL);
  if (!cleaned_path) { return NULL; }

  // check if path is still in www directory
  if (strncmp(cleaned_path, WEB_ROOT, strlen(WEB_ROOT)) != 0) {
    free(cleaned_path);
    return NULL;
  }

  printf("Cleaned path: %s\n", cleaned_path);

  // finally this is the real safe path
  // this memory must be freed by the caller of this function
  return cleaned_path; 
}
*/

// reads and serves requested file
void serve_file(int socket, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    // 404 not found
    char *not_found_body = "<html><body><h1>404 Not Found</h1></body></html>";
    // not using get_mime_type() here just because we don't know the file extension

    HttpResponse response = {
      .status = "404 Not Found",
      .content_type = "text/html",
      .body = not_found_body,
      .body_length = strlen(not_found_body),
      .headers = NULL,
      .num_headers = 0,
    };

    send_response(socket, &response); 
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


