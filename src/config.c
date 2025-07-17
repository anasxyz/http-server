#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/config.h"

#include "../include/config.h"

int SERVER_PORT = 8080;
const char *BACKEND_HOST = "127.0.0.1";
int BACKEND_PORT = 5000;

const char *WEB_ROOT = "www/";

Route routes[] = {
  { "/api/", BACKEND_HOST, BACKEND_PORT },
  { "/admin/", BACKEND_HOST, BACKEND_PORT },
};

const size_t num_routes = sizeof(routes) / sizeof(Route);

void load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Could not open config file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        // Trim whitespace/newline
        key[strcspn(key, "\r\n")] = '\0';
        value[strcspn(value, "\r\n")] = '\0';

        if (strcmp(key, "SERVER_PORT") == 0) {
            SERVER_PORT = atoi(value);
        } else if (strcmp(key, "BACKEND_PORT") == 0) {
            BACKEND_PORT = atoi(value);
        } else if (strcmp(key, "BACKEND_HOST") == 0) {
            BACKEND_HOST = strdup(value); // careful: strdup mallocs
        }
    }

    fclose(f);
}
