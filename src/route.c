#include <stdlib.h>
#include <string.h>

#include "../include/config.h"

#include "../include/route.h"

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

