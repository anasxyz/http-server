#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

#define MAX_LINE_LENGTH 1024

typedef enum { GLOBAL, HTTP, SERVER, LOCATION, SSL } parser_state_e;

config *global_config;

void init_config() {
  global_config = malloc(sizeof(config));
  if (global_config == NULL) {
    logs('E', "Couldn't allocate memory for top-level config.",
         "init_config() failed.");
    exit(1);
  }
  memset(global_config, 0, sizeof(config));

  global_config->http = malloc(sizeof(http_config));
  if (global_config->http == NULL) {
    logs('E', "Couldn't allocate memory for http config.",
         "init_config() failed.");
    free(global_config); // Clean up
    exit(1);
  }
  memset(global_config->http, 0, sizeof(http_config));
}

char *trim(char *str) {
  while (isspace((unsigned char)*str)) {
    str++;
  }
  if (*str == 0) {
    return str;
  }

  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) {
    end--;
  }
  *(end + 1) = 0;

  return str;
}

char **parse_string_list(const char *value, int *count) {
  char *temp_value = strdup(value);
  if (temp_value == NULL) {
    *count = 0;
    return NULL;
  }

  char **list = NULL;
  *count = 0;
  char *token = strtok(temp_value, ",");

  while (token != NULL) {
    char *name = trim(token);
    if (*name != '\0') {
      list = realloc(list, sizeof(char *) * (*count + 1));
      if (list == NULL) {
        // handle realloc failure
        for (int i = 0; i < *count; i++) {
          free(list[i]);
        }
        free(temp_value);
        *count = 0;
        return NULL;
      }
      list[*count] = strdup(name);
      (*count)++;
    }
    token = strtok(NULL, ",");
  }

  free(temp_value);
  return list;
}

long parse_duration_ms(const char *str) {
  if (!str || !*str)
    return -1; // empty string

  char *end;
  long value = strtol(str, &end, 10); // parse the number part

  if (end == str) {
    return -1; // no number found
  }

  // skip spaces after number
  while (isspace((unsigned char)*end)) {
    end++;
  }

  // check unit
  if (strncmp(end, "ms", 2) == 0) {
    return value;
  } else if (*end == 's' && *(end + 1) == '\0') {
    return value * 1000;
  } else if (*end == 'm' && *(end + 1) == '\0') {
    return value * 60 * 1000;
  } else if (*end == 'h' && *(end + 1) == '\0') {
    return value * 60 * 60 * 1000;
  } else if (*end == '\0') {
    // default to ms if no unit
    return value;
  }

  return -1; // unknown unit
}

void parse_config() {
  FILE *file = fopen("server.conf", "r");
  if (file == NULL) {
    logs('E', "Couldn't open server.conf.", "parse_config(): fopen() failed.");
    exits();
  }

  char line[MAX_LINE_LENGTH];
  parser_state_e state = GLOBAL;
  int num_servers = 0;
  server_config *current_server = NULL;

  while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
    char *colon_pos = strchr(line, ':');
    if (colon_pos == NULL) {
      continue;
    }

    *colon_pos = '\0';
    char *key = trim(line);
    char *value = trim(colon_pos + 1);

    if (key[0] == '\0' || key[0] == '#') {
      continue;
    }

    if (state == GLOBAL) {
      if (strcmp(key, "worker_processes") == 0) {
        global_config->worker_processes = atoi(value);
      } else if (strcmp(key, "user") == 0) {
        global_config->user = strdup(value);
      } else if (strcmp(key, "pid_file") == 0) {
        global_config->pid_file = strdup(value);
      } else if (strcmp(key, "log_file") == 0) {
        global_config->log_file = strdup(value);
      } else if (strcmp(key, "http.new") == 0) {
        state = HTTP; // we are now in the http block
      }
    }

    if (state == HTTP) {
      if (strcmp(key, "mime") == 0) {
        global_config->http->mime_types_path = strdup(value);
      } else if (strcmp(key, "default_type") == 0) {
        global_config->http->default_type = strdup(value);
      } else if (strcmp(key, "access_log_path") == 0) {
        global_config->http->access_log_path = strdup(value);
      } else if (strcmp(key, "error_log_path") == 0) {
        global_config->http->error_log_path = strdup(value);
      } else if (strcmp(key, "log_format") == 0) {
        global_config->http->log_format = strdup(value);
      } else if (strcmp(key, "server.new") == 0) {
        // we are now in a server block
        state = SERVER;
        // increment the number of servers
        num_servers++;

        // resize the servers array
        global_config->http->servers = realloc(
            global_config->http->servers, sizeof(server_config) * num_servers);
        if (global_config->http->servers == NULL) {
          logs('E', "Couldn't allocate memory for servers array.",
               "parse_config(): realloc() failed.");
          exits();
        }

        // get a pointer to the current server and initialise it
        current_server = &global_config->http->servers[num_servers - 1];
        memset(current_server, 0, sizeof(server_config));

        continue;
      }
    }

    if (state == SERVER) {
      if (strcmp(key, "port") == 0) {
        current_server->listen_port = atoi(value);
      } else if (strcmp(key, "server_name") == 0) {
        current_server->server_names = parse_string_list(value, &current_server->num_server_names);
        if (current_server->server_names == NULL && current_server->num_server_names != 0) {
          logs('E', "Couldn't allocate memory for server names.", "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "content_dir") == 0) {
        current_server->content_dir = strdup(value);
      } else if (strcmp(key, "index_files") == 0) {
        current_server->index_files = parse_string_list(value, &current_server->num_index_files);
        if (current_server->index_files == NULL && current_server->num_index_files != 0) {
          logs('E', "Couldn't allocate memory for index files.", "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "access_log_path") == 0) {
        current_server->access_log_path = strdup(value);
      } else if (strcmp(key, "error_log_path") == 0) {
        current_server->error_log_path = strdup(value);
      } else if (strcmp(key, "log_format") == 0) {
        current_server->log_format = strdup(value);
      } else if (strcmp(key, "timeout") == 0) {
        current_server->timeout = parse_duration_ms(value);
      } else if (strcmp(key, "ssl.new") == 0) {
        // we are now in a ssl block
        state = SSL;

				current_server->ssl = malloc(sizeof(ssl_config));
				if (current_server->ssl == NULL) {
					logs('E', "Couldn't allocate memory for ssl config.", "parse_config() failed.");
					exits();
				}
				memset(current_server->ssl, 0, sizeof(ssl_config));

        continue;
      } else if (strcmp(key, "location.new") == 0) {
        // we are now in a location block
        state = LOCATION;
        // increment the number of locations
        current_server->num_locations++;

        // resize the locations array
        current_server->locations = realloc(
            current_server->locations, sizeof(location_config) * current_server->num_locations);
        if (current_server->locations == NULL) {
          logs('E', "Couldn't allocate memory for locations array.",
               "parse_config(): realloc() failed.");
          exits();
        }

        // get a pointer to the current location and initialise it
        location_config *current_location = &current_server->locations[current_server->num_locations - 1];
        memset(current_location, 0, sizeof(location_config));

        continue;
      } else if (strcmp(key, "location.end") == 0) {
        state = SERVER;
      }
    }
	
		if (state == SSL) {
			if (strcmp(key, "cert_file") == 0) {
				current_server->ssl->cert_file = strdup(value);
			} else if (strcmp(key, "key_file") == 0) {
				current_server->ssl->key_file = strdup(value);
			} else if (strcmp(key, "protocols") == 0) {
				current_server->ssl->protocols = parse_string_list(value, &current_server->ssl->num_protocols);
				if (current_server->ssl->protocols == NULL && current_server->ssl->num_protocols != 0) {
					logs('E', "Couldn't allocate memory for ssl protocols.", "parse_config() failed.");
					exits();
				}
			} else if (strcmp(key, "ciphers") == 0) {
				current_server->ssl->ciphers = parse_string_list(value, &current_server->ssl->num_ciphers);
				if (current_server->ssl->ciphers == NULL && current_server->ssl->num_ciphers != 0) {
					logs('E', "Couldn't allocate memory for ssl ciphers.", "parse_config() failed.");
					exits();
				}
			} else if (strcmp(key, "ssl.end") == 0) {
				state = SERVER;
			}
		}
  }

  global_config->http->num_servers = num_servers;
  fclose(file);
}

void free_config() {
  // TODO: implement
}
