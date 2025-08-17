#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

config *global_config;

void init_config() {
  // init top-level config struct
  global_config = (config *)malloc(sizeof(config));
  if (global_config == NULL) {
    logs('E', "Couldn't allocate memory for config.",
         "init_config(): global_config malloc() failed.");
    exit(1);
  }

  // init top-level config struct attributes
  global_config->worker_processes = 4;
  global_config->user = NULL;
  global_config->pid_file = NULL;
  global_config->log_file = NULL;

  // init http config struct
  global_config->http = (http_config *)malloc(sizeof(http_config));
  if (global_config->http == NULL) { // Corrected check
    logs('E', "Couldn't allocate memory for http config.",
         "init_config(): global_config->http malloc() failed.");
    free(global_config);
    exit(1);
  }

  // init http struct attributes
  global_config->http->mime_types_path = NULL;
  global_config->http->default_type = NULL;
  global_config->http->access_log_path = NULL;
  global_config->http->error_log_path = NULL;
  global_config->http->log_format = NULL;
  global_config->http->servers = NULL;
  global_config->http->num_servers = 0;

  // hardcoded server config for testing (remove later and actually use parse_config())
  global_config->http->num_servers = 3;
  global_config->http->servers = (server_config *)malloc(
      sizeof(server_config) * global_config->http->num_servers);
  if (global_config->http->servers == NULL) {
    logs('E', "Couldn't allocate memory for servers array.",
         "parse_config(): servers malloc() failed.");
    free(global_config->http); // Added cleanup
    free(global_config); // Added cleanup
    exit(1);
  }

  // Loop to initialize each server
  for (int i = 0; i < global_config->http->num_servers; i++) {
    // Allocate memory for the server_names array
    // We'll hardcode space for 1 name for now
    global_config->http->servers[i].server_names = (char **)malloc(sizeof(char *) * 1);
    if (global_config->http->servers[i].server_names == NULL) {
        logs('E', "Couldn't allocate memory for server_names.",
             "init_config(): server_names malloc() failed.");
        // Add cleanup code for previously allocated memory
        exit(1);
    }
  }

  global_config->http->servers[0].listen_port = 8080;
  global_config->http->servers[0].server_names[0] = strdup("example1.com");
  global_config->http->servers[1].listen_port = 8443;
  global_config->http->servers[1].server_names[0] = strdup("example2.com");
  global_config->http->servers[2].listen_port = 9090;
  global_config->http->servers[2].server_names[0] = strdup("example3.com");

  // In check_valid_config(), the change to servers[i].server_names[0] is correct now.
}

void parse_config() {
  // TODO: implement
}

void free_config() {
  // TODO: implement
}
