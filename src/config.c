// config.c (UPDATED)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h> // for pthread_rwlock_t (if used for config ptr)
#include <stdarg.h>  // for variadic functions
#include <time.h>    // for time in logs
#include <unistd.h>  // for close()
#include <fcntl.h>   // for open() flags (O_CREAT, O_WRONLY, O_APPEND)

#include "../include/config.h" // Include the updated header

// global pointer to the active configuration
// use a mutex/read-write lock for thread-safe access if you introduce worker threads later
static Config *current_config_ptr = NULL;

// (rest of trim function is unchanged)
void trim(char *str) {
  char *end;

  while (isspace((unsigned char)*str)) str++;

  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  *(end + 1) = '\0';
}

// helper to open log files
static void open_log_files(Logger *logger) {
    // close existing fds if they are open (for reload)
    if (logger->access_log_fd != -1) {
        close(logger->access_log_fd);
        logger->access_log_fd = -1;
    }
    if (logger->error_log_fd != -1) {
        close(logger->error_log_fd);
        logger->error_log_fd = -1;
    }

    // open access log
    if (logger->access_log_path) {
        logger->access_log_fd = open(logger->access_log_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (logger->access_log_fd == -1) {
            perror("failed to open access log file");
            // continue without access log
        }
    } else {
        // default to stdout if no path specified, for simple cases
        logger->access_log_fd = STDOUT_FILENO;
    }


    // open error log
    if (logger->error_log_path) {
        logger->error_log_fd = open(logger->error_log_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (logger->error_log_fd == -1) {
            perror("failed to open error log file");
            // default to stderr if no path specified
            logger->error_log_fd = STDERR_FILENO;
        }
    } else {
        // default to stderr if no path specified
        logger->error_log_fd = STDERR_FILENO;
    }
}

// helper to close log files
static void close_log_files(Logger *logger) {
    // only close if it's not stdout/stderr
    if (logger->access_log_fd != -1 && logger->access_log_fd != STDOUT_FILENO) {
        close(logger->access_log_fd);
    }
    if (logger->error_log_fd != -1 && logger->error_log_fd != STDERR_FILENO) {
        close(logger->error_log_fd);
    }
    logger->access_log_fd = -1;
    logger->error_log_fd = -1;
}


// function to load config into a provided config struct
// initializes the struct and populates it. returns 1 on success, 0 on failure.
int load_config_into_struct(const char *path, Config *cfg) {
    // initialize the struct members to safe defaults / null
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
    // initialize logger paths and fds
    cfg->logger.access_log_path = NULL;
    cfg->logger.error_log_path = NULL;
    cfg->logger.access_log_fd = -1;
    cfg->logger.error_log_fd = -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("failed to open config file for reload");
        return 0; // indicate failure
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
        } else if (strcmp(key, "access_log") == 0) { // new config option for access log
            char *val = strtok(NULL, " ");
            if (val) cfg->logger.access_log_path = strdup(val);
        } else if (strcmp(key, "error_log") == 0) { // new config option for error log
            char *val = strtok(NULL, " ");
            if (val) cfg->logger.error_log_path = strdup(val);
        }
    }

    fclose(fp);

    // open log files after parsing config
    open_log_files(&cfg->logger);

    // print final parsed config for debugging (for the new config)
    printf("----- parsed config (%s)-----\n", path);
    printf("port: %d\n", cfg->port);
    printf("root: %s\n", cfg->root ? cfg->root : "(none)");

    printf("index files:\n");
    for (size_t i = 0; i < cfg->index_files_count; i++)
        printf("  - %s\n", cfg->index_files[i]);

    printf("try files:\n");
    for (size_t i = 0; i < cfg->try_files_count; i++)
        printf("  - %s\n", cfg->try_files[i]);

    printf("aliases:\n");
    for (size_t i = 0; i < cfg->aliases_count; i++)
        printf("  - %s => %s\n", cfg->aliases[i].from, cfg->aliases[i].to);

    printf("proxies:\n");
    for (size_t i = 0; i < cfg->proxies_count; i++)
        printf("  - %s => %s\n", cfg->proxies[i].from, cfg->proxies[i].to);

    printf("access log: %s (fd: %d)\n", cfg->logger.access_log_path ? cfg->logger.access_log_path : "(stdout)", cfg->logger.access_log_fd);
    printf("error log: %s (fd: %d)\n", cfg->logger.error_log_path ? cfg->logger.error_log_path : "(stderr)", cfg->logger.error_log_fd);

    printf("-------------------------\n");

    return 1; // indicate success
}

// function to free memory associated with a config struct
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
    free(cfg->logger.access_log_path); // free log paths
    free(cfg->logger.error_log_path);

    close_log_files(&cfg->logger); // close log file descriptors

    free(cfg); // free the config struct itself
}

// functions to get/set the globally active configuration
Config *get_current_config() {
    return current_config_ptr;
}

void set_current_config(Config *new_config) {
    // when setting a new config, ensure the old one's log fds are closed
    // before the old config struct itself is freed in main.c
    // free_global_config() handles this now
    current_config_ptr = new_config;
}

// function to free the currently active global config
void free_global_config() {
    if (current_config_ptr) {
        free_config_struct(current_config_ptr); // this will also close fds
    }
    current_config_ptr = NULL; // prevent double free
}


// new: logging functions

// get current time in a format suitable for logs
static char* get_log_time() {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now); // use localtime for local timezone in logs
    static char time_buf[64]; // static buffer for simplicity, not thread-safe if called concurrently
    strftime(time_buf, sizeof(time_buf), "[%d/%b/%Y:%H:%M:%S %z]", tm);
    return time_buf;
}

void log_access(const char *format, ...) {
    Config *cfg = get_current_config();
    if (!cfg || cfg->logger.access_log_fd == -1) {
        return; // cannot log if config or fd is not set
    }

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%d/%b/%Y:%H:%M:%S %z", localtime(&(time_t){time(NULL)}));

    dprintf(cfg->logger.access_log_fd, "%s - - ", get_log_time()); // common log format start

    va_list args;
    va_start(args, format);
    vdprintf(cfg->logger.access_log_fd, format, args);
    va_end(args);

    dprintf(cfg->logger.access_log_fd, "\n"); // newline for each log entry
}

void log_error(const char *format, ...) {
    Config *cfg = get_current_config();
    if (!cfg || cfg->logger.error_log_fd == -1) {
        return; // cannot log if config or fd is not set
    }

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%d/%b/%Y:%H:%M:%S %z", localtime(&(time_t){time(NULL)}));

    dprintf(cfg->logger.error_log_fd, "%s [error] ", get_log_time());

    va_list args;
    va_start(args, format);
    vdprintf(cfg->logger.error_log_fd, format, args);
    va_end(args);

    dprintf(cfg->logger.error_log_fd, "\n"); // newline for each log entry
}
