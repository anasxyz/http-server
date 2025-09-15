#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"
#include "defaults.h"

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
    free(global_config);
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

long parse_buffer_size(const char *str) {
  if (!str || !*str) {
    return -1;
  }

  char *end;
  long value = strtol(str, &end, 10);

  if (end == str) {
    return -1;
  }

  while (isspace((unsigned char)*end)) {
    end++;
  }

  char unit_str[5];
  int i = 0;
  while (*end && i < 4) {
    unit_str[i++] = tolower((unsigned char)*end);
    end++;
  }
  unit_str[i] = '\0';

  if (strcmp(unit_str, "") == 0 || strcmp(unit_str, "b") == 0) {
    return value;
  } else if (strcmp(unit_str, "k") == 0 || strcmp(unit_str, "kb") == 0) {
    return value * 1024;
  } else if (strcmp(unit_str, "m") == 0 || strcmp(unit_str, "mb") == 0) {
    return value * 1024 * 1024;
  } else if (strcmp(unit_str, "g") == 0 || strcmp(unit_str, "gb") == 0) {
    return value * 1024 * 1024 * 1024;
  }

  return -1;
}

int parse_config(char *config_file_path) {
  FILE *file = fopen(config_file_path, "r");
  if (file == NULL) {
    logs('E', "Couldn't open server.conf.", "parse_config(): fopen() failed.");
    return -1;
  }

  char line[MAX_LINE_LENGTH];
  parser_state_e state = GLOBAL;
  int num_servers = 0;
  server_config *current_server = NULL;
  route_config *current_route = NULL;

  while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
    char *comment_pos = strchr(line, '#');
    if (comment_pos != NULL) {
      *comment_pos = '\0';
    }

    char *colon_pos = strchr(line, ':');
    char *key = NULL;
    char *value = NULL;

    if (colon_pos) {
      *colon_pos = '\0';
      key = trim(line);
      value = trim(colon_pos + 1);
    } else {
      key = trim(line);
      value = "";
    }

    if (key[0] == '\0') {
      continue;
    }

    if (state == GLOBAL) {
      if (strcmp(key, "max_connections") == 0) {
        global_config->max_connections = atoi(value);
      } else if (strcmp(key, "worker_processes") == 0) {
        global_config->worker_processes = atoi(value);
      } else if (strcmp(key, "user") == 0) {
        global_config->user = strdup(value);
      } else if (strcmp(key, "pid_file") == 0) {
        global_config->pid_file = strdup(value);
      } else if (strcmp(key, "log_file") == 0) {
        global_config->log_file = strdup(value);
      } else if (strcmp(key, "http.new") == 0) {
        state = HTTP; // we are now in the http block
        continue;
      }
    } else if (state == HTTP) {
      if (strcmp(key, "default_buffer_size") == 0) {
        global_config->http->default_buffer_size = parse_buffer_size(value);
      } else if (strcmp(key, "body_buffer_size") == 0) {
        global_config->http->body_buffer_size = parse_buffer_size(value);
      } else if (strcmp(key, "headers_buffer_size") == 0) {
        global_config->http->headers_buffer_size = parse_buffer_size(value);
      } else if (strcmp(key, "mime") == 0) {
        global_config->http->mime_types_path = strdup(value);
      } else if (strcmp(key, "default_type") == 0) {
        global_config->http->default_type = strdup(value);
      } else if (strcmp(key, "access_log") == 0) {
        global_config->http->access_log_path = strdup(value);
      } else if (strcmp(key, "error_log") == 0) {
        global_config->http->error_log_path = strdup(value);
      } else if (strcmp(key, "log_format") == 0) {
        global_config->http->log_format = strdup(value);
      } else if (strcmp(key, "sendfile") == 0) {
        global_config->http->sendfile = (strcmp(value, "on") == 0);
      } else if (strcmp(key, "host.new") == 0) {
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
      } else if (strcmp(key, "http.end") == 0) {
        state = GLOBAL;
        continue;
      }
    } else if (state == SERVER) {
      if (strcmp(key, "listen") == 0) {
        current_server->listen_port = atoi(value);
      } else if (strcmp(key, "name") == 0) {
        current_server->server_names =
            parse_string_list(value, &current_server->num_server_names);
        if (current_server->server_names == NULL &&
            current_server->num_server_names != 0) {
          logs('E', "Couldn't allocate memory for server names.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "content_dir") == 0) {
        current_server->content_dir = strdup(value);
      } else if (strcmp(key, "index_files") == 0) {
        current_server->index_files =
            parse_string_list(value, &current_server->num_index_files);
        if (current_server->index_files == NULL &&
            current_server->num_index_files != 0) {
          logs('E', "Couldn't allocate memory for index files.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "access_log") == 0) {
        current_server->access_log_path = strdup(value);
      } else if (strcmp(key, "error_log") == 0) {
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
          logs('E', "Couldn't allocate memory for ssl config.",
               "parse_config() failed.");
          exits();
        }
        memset(current_server->ssl, 0, sizeof(ssl_config));

        continue;
      } else if (strcmp(key, "route.new") == 0) {
        // we are now in a route block
        state = LOCATION;
        // increment the number of routes
        current_server->num_routes++;

        // resize the routes array
        current_server->routes =
            realloc(current_server->routes,
                    sizeof(route_config) * current_server->num_routes);
        if (current_server->routes == NULL) {
          logs('E', "Couldn't allocate memory for routes array.",
               "parse_config(): realloc() failed.");
          exits();
        }

        // get a pointer to the current route and initialise it
        current_route = &current_server->routes[current_server->num_routes - 1];
        memset(current_route, 0, sizeof(route_config));

        continue;
      } else if (strcmp(key, "host.end") == 0) {
        state = HTTP;
        continue;
      }
    } else if (state == SSL) {
      if (strcmp(key, "cert_file") == 0) {
        current_server->ssl->cert_file = strdup(value);
      } else if (strcmp(key, "key_file") == 0) {
        current_server->ssl->key_file = strdup(value);
      } else if (strcmp(key, "protocols") == 0) {
        current_server->ssl->protocols =
            parse_string_list(value, &current_server->ssl->num_protocols);
        if (current_server->ssl->protocols == NULL &&
            current_server->ssl->num_protocols != 0) {
          logs('E', "Couldn't allocate memory for ssl protocols.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "ciphers") == 0) {
        current_server->ssl->ciphers =
            parse_string_list(value, &current_server->ssl->num_ciphers);
        if (current_server->ssl->ciphers == NULL &&
            current_server->ssl->num_ciphers != 0) {
          logs('E', "Couldn't allocate memory for ssl ciphers.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "ssl.end") == 0) {
        state = SERVER;
        continue;
      }
    } else if (state == LOCATION) {
      if (strcmp(key, "uri") == 0) {
        current_route->uri = strdup(value);
      } else if (strcmp(key, "content_dir") == 0) {
        current_route->content_dir = strdup(value);
      } else if (strcmp(key, "index_files") == 0) {
        current_route->index_files =
            parse_string_list(value, &current_route->num_index_files);
        if (current_route->index_files == NULL &&
            current_route->num_index_files != 0) {
          logs('E', "Couldn't allocate memory for index files.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "proxy_url") == 0) {
        current_route->proxy_url = strdup(value);
      } else if (strcmp(key, "autoindex") == 0) {
        current_route->autoindex = (strcmp(value, "on") == 0);
      } else if (strcmp(key, "allow") == 0) {
        // TODO: handle CIDR notation
        // TODO: maybe read it from a file?
        current_route->allowed_ips =
            parse_string_list(value, &current_route->num_allowed_ips);
        if (current_route->allowed_ips == NULL &&
            current_route->num_allowed_ips != 0) {
          logs('E', "Couldn't allocate memory for allowed ips.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "deny") == 0) {
        // TODO: handle CIDR notation
        // TODO: maybe read it from a file?
        current_route->denied_ips =
            parse_string_list(value, &current_route->num_denied_ips);
        if (current_route->denied_ips == NULL &&
            current_route->num_denied_ips != 0) {
          logs('E', "Couldn't allocate memory for denied ips.",
               "parse_config() failed.");
          exits();
        }
      } else if (strcmp(key, "return") == 0) {
        current_route->return_status = atoi(value);
      } else if (strcmp(key, "redirect") == 0) {
        current_route->return_url_text = strdup(value);
      } else if (strcmp(key, "etag_header") == 0) {
        current_route->etag_header = strdup(value);
      } else if (strcmp(key, "expires_header") == 0) {
        current_route->expires_header = strdup(value);
      } else if (strcmp(key, "route.end") == 0) {
        state = SERVER;
        continue;
      }
    }
  }

  global_config->http->num_servers = num_servers;
  fclose(file);

  return 0;
}

void load_config(char *config_file_path) {
  init_config();
  if (parse_config(config_file_path) == -1) {
    printf("Configuration file not found.\n");
    exits();
  }
}

void free_config() {
  if (global_config == NULL) {
    return;
  }

  if (global_config->user)
    free(global_config->user);
  if (global_config->pid_file)
    free(global_config->pid_file);
  if (global_config->log_file)
    free(global_config->log_file);

  if (global_config->http != NULL) {
    if (global_config->http->mime_types_path)
      free(global_config->http->mime_types_path);
    if (global_config->http->default_type)
      free(global_config->http->default_type);
    if (global_config->http->access_log_path)
      free(global_config->http->access_log_path);
    if (global_config->http->error_log_path)
      free(global_config->http->error_log_path);
    if (global_config->http->log_format)
      free(global_config->http->log_format);

    if (global_config->http->servers) {
      for (int i = 0; i < global_config->http->num_servers; i++) {
        server_config *server = &global_config->http->servers[i];

        if (server->content_dir)
          free(server->content_dir);
        if (server->access_log_path)
          free(server->access_log_path);
        if (server->error_log_path)
          free(server->error_log_path);
        if (server->log_format)
          free(server->log_format);

        if (server->server_names) {
          for (int j = 0; j < server->num_server_names; j++) {
            if (server->server_names[j]) {
              free(server->server_names[j]);
            }
          }
          free(server->server_names);
        }

        if (server->index_files) {
          for (int j = 0; j < server->num_index_files; j++) {
            if (server->index_files[j]) {
              free(server->index_files[j]);
            }
          }
          free(server->index_files);
        }

        if (server->ssl != NULL) {
          if (server->ssl->cert_file)
            free(server->ssl->cert_file);
          if (server->ssl->key_file)
            free(server->ssl->key_file);

          if (server->ssl->protocols) {
            for (int j = 0; j < server->ssl->num_protocols; j++) {
              if (server->ssl->protocols[j]) {
                free(server->ssl->protocols[j]);
              }
            }
            free(server->ssl->protocols);
          }

          if (server->ssl->ciphers) {
            for (int j = 0; j < server->ssl->num_ciphers; j++) {
              if (server->ssl->ciphers[j]) {
                free(server->ssl->ciphers[j]);
              }
            }
            free(server->ssl->ciphers);
          }

          free(server->ssl);
        }

        if (server->routes) {
          for (int j = 0; j < server->num_routes; j++) {
            route_config *route = &server->routes[j];

            if (route->uri)
              free(route->uri);
            if (route->content_dir)
              free(route->content_dir);
            if (route->proxy_url)
              free(route->proxy_url);
            if (route->return_url_text)
              free(route->return_url_text);
            if (route->etag_header)
              free(route->etag_header);
            if (route->expires_header)
              free(route->expires_header);

            if (route->index_files) {
              for (int k = 0; k < route->num_index_files; k++) {
                if (route->index_files[k]) {
                  free(route->index_files[k]);
                }
              }
              free(route->index_files);
            }

            if (route->allowed_ips) {
              for (int k = 0; k < route->num_allowed_ips; k++) {
                if (route->allowed_ips[k]) {
                  free(route->allowed_ips[k]);
                }
              }
              free(route->allowed_ips);
            }

            if (route->denied_ips) {
              for (int k = 0; k < route->num_denied_ips; k++) {
                if (route->denied_ips[k]) {
                  free(route->denied_ips[k]);
                }
              }
              free(route->denied_ips);
            }
          }
          free(server->routes);
        }
      }
      free(global_config->http->servers);
    }
    free(global_config->http);
  }

  free(global_config);
  global_config = NULL;
}

// TEMPORARILY INCOMPLETE
void check_config() {
  if (is_empty(global_config->pid_file)) {
    global_config->pid_file = DEFAULT_PID_FILE;
  }

	if (is_empty(global_config->log_file)) {
		global_config->log_file = DEFAULT_LOG_FILE;
	}

	if (is_empty(global_config->http->mime_types_path)) {
		printf("MIME IS EMPTY IN CONFIG, USING DEFAULT BUILT IN %s\n", DEFAULT_MIME_PATH);
		global_config->http->mime_types_path = DEFAULT_MIME_PATH;
	} else {
		printf("MIME IS NOT EMPTY IN CONFIG, USING %s\n", global_config->http->mime_types_path);
	}
}
