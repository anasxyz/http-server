// config.c (UPDATED)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h> // For pthread_rwlock_t

#include "../include/config.h" // Include the updated header

// Global pointer to the active configuration
// Use a mutex/read-write lock for thread-safe access if you introduce worker threads later
static Config *current_config_ptr = NULL;

// Add a lock for thread-safe access to the current_config_ptr if multi-threaded
// For this single-threaded epoll loop, a volatile pointer swap is generally safe
// if access is only from the main loop. But if 'http.c' functions could be called
// concurrently by worker threads, this would need a lock.
// For now, let's keep it simple with volatile for `reload_config_flag`
// and rely on the single-threaded nature of the epoll loop for config access.
// If your design later includes a thread pool, you'd need a `pthread_rwlock_t`
// for `current_config_ptr` reads/writes.


void trim(char *str) {
  char *end;

  while (isspace((unsigned char)*str)) str++;

  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  *(end + 1) = '\0';
}

// Function to load config into a provided Config struct
// Initializes the struct and populates it. Returns 1 on success, 0 on failure.
int load_config_into_struct(const char *path, Config *cfg) {
    // Initialize the struct members to safe defaults / NULL
    cfg->port = 8080;
    cfg->root = NULL;
    cfg->index_files = NULL;
    cfg->index_files_count = 0;
    cfg->try_files = NULL;
    cfg->try_files_count = 0;
    cfg->aliases = NULL;
    cfg->aliases_count = 0;
    cfg->proxies = NULL;
    cfg->proxies_count = 0;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("Failed to open config file for reload");
        return 0; // Indicate failure
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '#' || strlen(line) == 0)
            continue;

        char *key = strtok(line, " ");
        if (!key) continue;

        if (strcmp(key, "port") == 0) {
            char *val = strtok(NULL, " ");
            if (val) cfg->port = atoi(val);
        } else if (strcmp(key, "root") == 0) {
            char *val = strtok(NULL, " ");
            if (val) cfg->root = strdup(val);
        } else if (strcmp(key, "index") == 0) {
            char *val;
            while ((val = strtok(NULL, " "))) {
                cfg->index_files = realloc(cfg->index_files, sizeof(char *) * (cfg->index_files_count + 1));
                if (!cfg->index_files) { perror("realloc index_files"); fclose(fp); return 0; }
                cfg->index_files[cfg->index_files_count++] = strdup(val);
            }
        } else if (strcmp(key, "try_files") == 0) {
            char *val;
            while ((val = strtok(NULL, " "))) {
                cfg->try_files = realloc(cfg->try_files, sizeof(char *) * (cfg->try_files_count + 1));
                if (!cfg->try_files) { perror("realloc try_files"); fclose(fp); return 0; }
                cfg->try_files[cfg->try_files_count++] = strdup(val);
            }
        } else if (strcmp(key, "alias") == 0) {
            char *from = strtok(NULL, " ");
            char *to = strtok(NULL, " ");
            if (from && to) {
                cfg->aliases = realloc(cfg->aliases, sizeof(Alias) * (cfg->aliases_count + 1));
                if (!cfg->aliases) { perror("realloc aliases"); fclose(fp); return 0; }
                cfg->aliases[cfg->aliases_count].from = strdup(from);
                cfg->aliases[cfg->aliases_count].to = strdup(to);
                cfg->aliases_count++;
            }
        } else if (strcmp(key, "proxy") == 0) {
            char *from = strtok(NULL, " ");
            char *to = strtok(NULL, " ");
            if (from && to) {
                cfg->proxies = realloc(cfg->proxies, sizeof(Proxy) * (cfg->proxies_count + 1));
                if (!cfg->proxies) { perror("realloc proxies"); fclose(fp); return 0; }
                cfg->proxies[cfg->proxies_count].from = strdup(from);
                cfg->proxies[cfg->proxies_count].to = strdup(to);
                cfg->proxies_count++;
            }
        }
    }

    fclose(fp);

    // Print final parsed config for debugging (for the new config)
    printf("----- Parsed Config (%s)-----\n", path);
    printf("Port: %d\n", cfg->port);
    printf("Root: %s\n", cfg->root ? cfg->root : "(none)");

    printf("Index Files:\n");
    for (size_t i = 0; i < cfg->index_files_count; i++)
        printf("  - %s\n", cfg->index_files[i]);

    printf("Try Files:\n");
    for (size_t i = 0; i < cfg->try_files_count; i++)
        printf("  - %s\n", cfg->try_files[i]);

    printf("Aliases:\n");
    for (size_t i = 0; i < cfg->aliases_count; i++)
        printf("  - %s => %s\n", cfg->aliases[i].from, cfg->aliases[i].to);

    printf("Proxies:\n");
    for (size_t i = 0; i < cfg->proxies_count; i++)
        printf("  - %s => %s\n", cfg->proxies[i].from, cfg->proxies[i].to);

    printf("-------------------------\n");

    return 1; // Indicate success
}

// Function to free memory associated with a Config struct
void free_config_struct(Config *cfg) {
    if (!cfg) return;

    for (size_t i = 0; i < cfg->index_files_count; i++)
        free(cfg->index_files[i]);
    free(cfg->index_files);

    for (size_t i = 0; i < cfg->try_files_count; i++)
        free(cfg->try_files[i]);
    free(cfg->try_files);

    for (size_t i = 0; i < cfg->aliases_count; i++) {
        free(cfg->aliases[i].from);
        free(cfg->aliases[i].to);
    }
    free(cfg->aliases);

    for (size_t i = 0; i < cfg->proxies_count; i++) {
        free(cfg->proxies[i].from);
        free(cfg->proxies[i].to);
    }
    free(cfg->proxies);

    free(cfg->root);
    free(cfg); // Free the Config struct itself
}

// Functions to get/set the globally active configuration
Config *get_current_config() {
    return current_config_ptr;
}

void set_current_config(Config *new_config) {
    current_config_ptr = new_config;
}

// Function to free the currently active global config
void free_global_config() {
    free_config_struct(current_config_ptr);
    current_config_ptr = NULL; // Prevent double free
}
