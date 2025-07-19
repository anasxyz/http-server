#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/utils_path.h"

// default values
int SERVER_PORT;
char *WEB_ROOT = NULL;
Route *routes = NULL;
size_t num_routes = 0;

void load_config(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    perror("Could not open config file");
    return;
  }

  char line[512];
  size_t route_capacity = 4;
  routes = malloc(sizeof(Route) * route_capacity);
  if (!routes) {
    perror("Failed to allocate initial routes");
    fclose(f);
    exit(1);
  }

  while (fgets(line, sizeof(line), f)) {
    char *eq = strchr(line, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;

    // Trim newline/whitespace
    key[strcspn(key, "\r\n")] = '\0';
    value[strcspn(value, "\r\n")] = '\0';

    if (strcmp(key, "SERVER_PORT") == 0) {
      SERVER_PORT = atoi(value);
    } else if (strcmp(key, "WEB_ROOT") == 0) {
      WEB_ROOT = strdup(value); // Assume clean_path exists if needed
    } else if (strcmp(key, "ROUTE") == 0) {
      // Expected format: /prefix/,URL=http://host[:port][/backend_path/]
      char *comma = strchr(value, ',');
      if (!comma)
        continue;

      *comma = '\0';
      char *prefix = clean_path(value);
      char *url_part = comma + 1;

      if (strncmp(url_part, "URL=http://", 11) != 0)
        continue;

      char *url = url_part + 11; // After "http://"
      char *host_start = url;

      // Look for path (first slash after host[:port])
      char *path_start = strchr(host_start, '/');
      char *host_port = NULL;
      if (path_start) {
        host_port = strndup(host_start, path_start - host_start);
      } else {
        host_port = strdup(host_start);
      }

      // Separate host and port
      char *port_start = strchr(host_port, ':');
      char *host = NULL;
      int port = 80; // Default

      if (port_start) {
        *port_start = '\0';
        host = strdup(host_port);
        port = atoi(port_start + 1);
      } else {
        host = strdup(host_port);
      }
      free(host_port);

      // backend path
      char *backend_path = path_start ? strdup(path_start) : strdup("/");

      // Resize if needed
      if (num_routes >= route_capacity) {
        route_capacity *= 2;
        routes = realloc(routes, sizeof(Route) * route_capacity);
        if (!routes) {
          perror("Failed to realloc routes");
          fclose(f);
          exit(1);
        }
      }

      routes[num_routes].prefix = strdup(prefix);
      routes[num_routes].host = host;
      routes[num_routes].port = port;
      routes[num_routes].backend_path = backend_path;
      num_routes++;

      // free memory
      free(prefix);
    }
  }

  fclose(f);

  // Debug print
  printf("=== Loaded Config ===\n");
  printf("PORT = %d | ROOT = %s\n", SERVER_PORT, WEB_ROOT);
  printf("Routes (%zu):\n", num_routes);
  for (size_t i = 0; i < num_routes; i++) {
    printf("  [%zu] prefix='%s', host='%s', port=%d, backend_path='%s'\n",
           i + 1, routes[i].prefix, routes[i].host, routes[i].port,
           routes[i].backend_path);
  }
  printf("=====================\n");
}

void free_config() {
  if (WEB_ROOT) {
    free(WEB_ROOT);
    WEB_ROOT = NULL;
  }

  for (size_t i = 0; i < num_routes; i++) {
    free(routes[i].prefix);
    free(routes[i].host);
    free(routes[i].backend_path);
  }

  free(routes);
  routes = NULL;
  num_routes = 0;
}
