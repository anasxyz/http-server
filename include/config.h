#ifndef config_h
#define config_h

#include <stddef.h> 

typedef struct Alias Alias;
typedef struct Proxy Proxy;

typedef struct {
    char *access_log_path; // path to the access log file
    char *error_log_path;  // path to the error log file
    int access_log_fd;     // file descriptor for access log
    int error_log_fd;      // file descriptor for error log
} Logger;

// define the Config struct to hold all configuration parameters
typedef struct {
    int port;
    char *root;
    char **index_files;
    size_t index_files_count;
    char **try_files;
    size_t try_files_count;
    Alias *aliases;
    size_t aliases_count;
    Proxy *proxies;
    size_t proxies_count;
    Logger logger; // embed the logger struct directly
} Config;

struct Alias {
  char *from;
  char *to;
};

struct Proxy {
  char *from;
  char *to;
};

int load_config_into_struct(const char *path, Config *cfg);
void free_config_struct(Config *cfg);
Config *get_current_config();
void set_current_config(Config *new_config);
void free_global_config();
void log_access(const char *format, ...);
void log_error(const char *format, ...);

#endif // config_h
