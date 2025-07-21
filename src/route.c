#include <stdlib.h>
#include <string.h>

#include "../include/config.h"

#include "../include/route.h"

bool match_prefix(const char *path, const char *prefix) {
    size_t len = strlen(prefix);
    if (strncmp(path, prefix, len) != 0) return false;

    // if prefix ends in slash, match everything under it
    if (prefix[len - 1] == '/') return true;

    // else ensure it’s an exact word boundary
    return path[len] == '\0' || path[len] == '/';
}

Route* match_route(char *path) {
    for (size_t i = 0; i < ROUTES_COUNT; i++) {
        if (match_prefix(path, ROUTES[i].prefix)) {
            return &ROUTES[i];
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

        // Ensure leading slash
        size_t trimmed_len = strlen(path + prefix_len);
        char *result = malloc(trimmed_len + 2); // +1 for '/', +1 for '\0'
        if (!result) return NULL;

        result[0] = '/';
        strcpy(result + 1, path + prefix_len);
        return result;
    }

    return strdup(path);
}

