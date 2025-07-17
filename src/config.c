#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/utils_path.h"

// default values
int SERVER_PORT;
char *WEB_ROOT = "";
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
    if (!eq) continue;

    *eq = '\0';
    char *key = line;
    char *value = eq + 1;

    // Trim whitespace/newlines
    key[strcspn(key, "\r\n")] = '\0';
    value[strcspn(value, "\r\n")] = '\0';

    if (strcmp(key, "SERVER_PORT") == 0) {
      SERVER_PORT = atoi(value);
    } else if (strcmp(key, "WEB_ROOT") == 0) {
      WEB_ROOT = clean_path(value);
    } else if (strcmp(key, "ROUTE") == 0) {
      // value format: /prefix/,URL=http://host:port/whatever
      char *comma = strchr(value, ',');
      if (!comma) continue;

      *comma = '\0';
      char *prefix = value;
      char *url_part = comma + 1;

      if (strncmp(url_part, "URL=http://", 11) != 0) continue;
      char *url = url_part + 11;

      // Split url into host and port
      char *host_start = url;
      char *port_start = strchr(host_start, ':');
      if (!port_start) continue;

      *port_start = '\0';
      port_start++;

      // Optionally cut off the path after port
      char *slash = strchr(port_start, '/');
      if (slash) *slash = '\0';

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
      routes[num_routes].host = strdup(host_start);
      routes[num_routes].port = atoi(port_start);
      num_routes++;
    }
  }

  fclose(f);

  printf("=== Loaded Config ===\n");
  printf("PORT = %d\n", SERVER_PORT);
  printf("ROOT = %s\n", WEB_ROOT);
  printf("Routes (%zu):\n", num_routes);
  for (size_t i = 0; i < num_routes; i++) {
    printf("  Route %zu: prefix='%s', host='%s', port=%d\n", i + 1,
           routes[i].prefix, routes[i].host, routes[i].port);
  }
  printf("=====================\n");
}
