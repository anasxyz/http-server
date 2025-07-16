/*
 * NOT NEEDED FOR NOW
 *
 */

#include <stdlib.h>
#include <string.h>

#include "../include/route.h"
#include "../include/proxy.h"

Route routes[] = {
  { "/api/status", BACKEND_HOST, BACKEND_PORT },
  { "/api/files", BACKEND_HOST, BACKEND_PORT },
};

const size_t num_routes = sizeof(routes) / sizeof(Route);

Route* match_route(char *path) {
    for (size_t i = 0; i < num_routes; i++) {
        if (strncmp(path, routes[i].prefix, strlen(routes[i].prefix)) == 0) {
            return &routes[i];
        }
    }
    return NULL;
}

char *trim_prefix(const char *path, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) == 0) {
        // If it exactly matches the prefix, return "/"
        if (path[prefix_len] == '\0') {
            return strdup("/");
        }
        return strdup(path + prefix_len);
    }
    return strdup(path); // fallback (shouldn't happen if matched)
}

