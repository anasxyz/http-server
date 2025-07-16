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

bool match_prefix(const char *path, const char *prefix) {
    size_t len = strlen(prefix);
    if (strncmp(path, prefix, len) != 0) {
        return false;
    }

    return path[len] == '\0' || path[len] == '/';
}

Route* match_route(char *path) {
    for (size_t i = 0; i < num_routes; i++) {
        if (match_prefix(path, routes[i].prefix)) {
            return &routes[i];
        }
    }
    return NULL;
}

char *trim_prefix(const char *path, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) == 0) {
        if (path[prefix_len] == '\0') {
            return strdup("/");
        }
        return strdup(path + prefix_len);
    }
    return strdup(path);
}

