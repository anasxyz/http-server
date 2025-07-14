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

const char* get_final_path(const char *request_path) {
  static char full_path[1024];  // make it static so it can be safely returned
  const char *clean_request_path = clean_path(request_path);

  // if root path
  if (strcmp(clean_request_path, "/") == 0) {
    snprintf(full_path, sizeof(full_path), "%s/index.html", WEB_ROOT);
  } 
  // if ends with '/' assume directory and append index.html
  else if (clean_request_path[strlen(clean_request_path) - 1] == '/') {
    snprintf(full_path, sizeof(full_path), "%s%sindex.html", WEB_ROOT, clean_request_path);
  } 
  // else, it's a direct file
  else {
    snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, clean_request_path);
  }

  const char* final_path = clean_path(full_path);

  // for testing
  char* fake_path = "../../Desktop/test.html";
  printf("------------ IS THE PATH CLEAN? ----------\n");
  printf("Request Path: %s\n", request_path);
  printf("Cleaned Request Path: %s\n", clean_request_path);
  printf("Full Path: %s\n", full_path);
  printf("Cleaned Full Path (final path): %s\n", final_path);

  return final_path;
}
