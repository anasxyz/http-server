// include/config.h (UPDATED)
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h> // For size_t

// Forward declarations if needed for other files
typedef struct Alias Alias;
typedef struct Proxy Proxy;

// Define the Config struct to hold all configuration parameters
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
} Config;

// Alias and Proxy structs (if not already in separate headers)
struct Alias {
  char *from;
  char *to;
};

struct Proxy {
  char *from;
  char *to;
  // Potentially add target_host and target_port here from parsing "to" field
};


// Function to load config into a provided Config struct
// Returns 1 on success, 0 on failure
int load_config_into_struct(const char *path, Config *cfg);

// Function to free memory associated with a Config struct
void free_config_struct(Config *cfg);

// Functions to get/set the globally active configuration
Config *get_current_config();
void set_current_config(Config *new_config);

// Function to free the currently active global config
void free_global_config();

#endif // CONFIG_H
