#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

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

// Checks if path exists (file or directory)
bool does_path_exist(char *path) {
  struct stat st;
  return (stat(path, &st) == 0);
}

// Checks if path is a regular file
bool is_regular_file(char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return false;
  return S_ISREG(st.st_mode);
}

// Checks if path is a directory
bool is_directory(char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return false;
  return S_ISDIR(st.st_mode);
}

// this took ridiuculously long to figure out
// anytime this is called it should be freed by the caller
char *clean_path(char *path) {
  if (!path)
    return NULL;

  size_t len = strlen(path);
  if (len == 0)
    return strdup(".");

  bool had_trailing_slash = (path[len - 1] == '/');

  // Allocate room for components
  char **stack = malloc(sizeof(char *) * (len + 1));
  char *path_copy = strdup(path);
  char *token;
  size_t stack_size = 0;

  token = strtok(path_copy, "/");
  while (token != NULL) {
    if (strcmp(token, "..") == 0) {
      if (stack_size > 0) {
        stack_size--;
      }
    } else if (strcmp(token, ".") != 0 && strlen(token) > 0) {
      stack[stack_size++] = token;
    }
    token = strtok(NULL, "/");
  }

  char *cleaned = malloc(len + 2);
  if (!cleaned) return NULL;
  cleaned[0] = '\0';

  for (size_t i = 0; i < stack_size; ++i) {
    strcat(cleaned, stack[i]);
    if (i < stack_size - 1) {
      strcat(cleaned, "/");
    }
  }

  // Add single trailing slash back if it was present in original
  if (had_trailing_slash && strlen(cleaned) > 0 && cleaned[strlen(cleaned) - 1] != '/') {
    strcat(cleaned, "/");
  }

  // Handle empty path
  if (strlen(cleaned) == 0) {
    strcpy(cleaned, ".");
  }

  free(stack);
  free(path_copy);
  return cleaned;
}

char *get_full_path(char *request_path) {
  static char full_path[1024]; // make it static so it can be safely returned

  // if first 4 characters are "WEB_ROOT/" then just return it
  if (strncmp(request_path, WEB_ROOT "/", strlen(WEB_ROOT) + 1) == 0) {
    snprintf(full_path, sizeof(full_path), "%s", request_path);
  } else {
    snprintf(full_path, sizeof(full_path), "%s/%s", WEB_ROOT, request_path);
  }
  // clean it because the given path might be something like "www/"
  // and then we add another slash inbetween
  return full_path;
}

char *resolve_path(char *request_path) {
  static char resolved_path[1024];
  snprintf(resolved_path, sizeof(resolved_path), "%s", request_path);

  bool has_trailing_slash = resolved_path[strlen(resolved_path) - 1] == '/';

  // Case 1: If trailing slash → prioritize directory
  if (has_trailing_slash) {
    if (does_path_exist(resolved_path) && is_directory(resolved_path)) {
      // Ensure there's space for "index.html"
      if (strlen(resolved_path) + strlen("index.html") < sizeof(resolved_path)) {
        strcat(resolved_path, "index.html");
        if (does_path_exist(resolved_path) && is_regular_file(resolved_path)) {
          return resolved_path;
        }
      }
    }
    return NULL; // If directory/index.html not found, nothing else to do
  }

  // Case 2: No trailing slash → prioritize files

  // Exact match as regular file
  if (does_path_exist(resolved_path) && is_regular_file(resolved_path)) {
    return resolved_path;
  }

  // Try adding ".html"
  size_t len = strlen(resolved_path);
  if (len + 5 < sizeof(resolved_path)) {
    strcat(resolved_path, ".html");
    if (does_path_exist(resolved_path) && is_regular_file(resolved_path)) {
      return resolved_path;
    }
    resolved_path[len] = '\0'; // revert
  }

  // Try as directory + index.html
  if (does_path_exist(resolved_path) && is_directory(resolved_path)) {
    if (strlen(resolved_path) + strlen("/index.html") < sizeof(resolved_path)) {
      strcat(resolved_path, "/index.html");
      if (does_path_exist(resolved_path) && is_regular_file(resolved_path)) {
        return resolved_path;
      }
    }
  }

  return NULL;
}
