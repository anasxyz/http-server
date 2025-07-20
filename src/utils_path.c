#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/utils_path.h"

char *get_mime_type(const char *path) {
  const char *extension = strrchr(path, '.');
  if (extension == NULL) {
    return "application/octet-stream";
  }

  // TODO: add more MIME types
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
  if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
    return "text/html";
  }
  if (strcmp(extension, ".css") == 0) {
    return "text/css";
  }
  if (strcmp(extension, ".js") == 0) {
    return "text/javascript";
  }
  if (strcmp(extension, ".json") == 0) {
    return "application/json";
  }
  if (strcmp(extension, ".png") == 0) {
    return "image/png";
  }
  if ((strcmp(extension, ".jpg") == 0) || (strcmp(extension, ".jpeg") == 0)) {
    return "image/jpeg";
  }
  if (strcmp(extension, ".gif") == 0) {
    return "image/gif";
  }
  if (strcmp(extension, ".txt") == 0) {
    return "text/plain";
  }

  return "application/octet-stream"; // default MIME type for unknown extensions
}

char *join_paths(const char *root, const char *path) {
  if (!root || !path)
    return NULL;

  size_t root_len = strlen(root);
  size_t path_len = strlen(path);

  size_t total_len = root_len + path_len + 2; // possible '/' and '\0'
  char *result = malloc(total_len);
  if (!result)
    return NULL;

  strcpy(result, root);

  bool root_ends_slash = (root_len > 0 && root[root_len - 1] == '/');
  bool path_starts_slash = (path_len > 0 && path[0] == '/');

  if (root_ends_slash && path_starts_slash) {
    strcat(result, path + 1);
  } else if (!root_ends_slash && !path_starts_slash) {
    strcat(result, "/");
    strcat(result, path);
  } else {
    strcat(result, path);
  }

  return result;
}

// Helper to check if a file exists and is a regular file
bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

/**
 * try_files_path:
 * Tries a list of paths (relative to root) in order, returns the first existing file's full path.
 * Returns a newly allocated string that must be freed by the caller.
 * If none found, returns NULL.
 *
 * root: the root directory (e.g. "/var/www")
 * paths: an array of relative paths to try (e.g. {"index.html", "index.htm", "404.html"})
 * count: number of paths in the array
 */
char *try_files_path(const char *root, const char **paths, size_t count) {
    for (size_t i = 0; i < count; i++) {
        char *full_path = join_paths(root, paths[i]);
        if (!full_path) {
            continue; // malloc failed, skip
        }
        if (file_exists(full_path)) {
            return full_path; // caller owns this string, must free later
        }
        free(full_path);
    }
    return NULL; // none found
}
