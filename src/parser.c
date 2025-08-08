#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> 

#include "parser.h"
#include "server.h"

static void parse_all_headers(client_state_t *client_state, char *buffer) {
  char *header_start = strstr(buffer, "\r\n") + 2;
  char *header_line;
  char *saveptr;

  client_state->header_count = 0;

  char temp_buffer[MAX_BUFFER_SIZE];
  strncpy(temp_buffer, header_start, sizeof(temp_buffer) - 1);
  temp_buffer[sizeof(temp_buffer) - 1] = '\0';

  header_line = strtok_r(temp_buffer, "\r\n", &saveptr);
  while (header_line != NULL) {
    char *colon = strchr(header_line, ':');
    if (colon) {
      *colon = '\0';
      char *key = header_line;
      char *value = colon + 1;
      while (*value == ' ')
        value++;

      if (client_state->header_count < MAX_HEADERS) {
        strncpy(client_state->parsed_headers[client_state->header_count].key,
                key, MAX_HEADER_KEY_LEN - 1);
        client_state->parsed_headers[client_state->header_count]
            .key[MAX_HEADER_KEY_LEN - 1] = '\0';

        strncpy(client_state->parsed_headers[client_state->header_count].value,
                value, MAX_HEADER_VALUE_LEN - 1);
        client_state->parsed_headers[client_state->header_count]
            .value[MAX_HEADER_VALUE_LEN - 1] = '\0';

        client_state->header_count++;
      }
    }
    header_line = strtok_r(NULL, "\r\n", &saveptr);
  }
}

// A helper function to find a header value
const char *get_header_value(client_state_t *client_state, const char *key) {
  for (int i = 0; i < client_state->header_count; i++) {
    if (strcasecmp(client_state->parsed_headers[i].key, key) == 0) {
      return client_state->parsed_headers[i].value;
    }
  }
  return NULL;
}

// The main function to parse the entire request
// It now returns the next state instead of setting it directly.
// The main function to parse the entire request
int parse_http_request(client_state_t *client_state) {
  char *header_end = strstr(client_state->in_buffer, "\r\n\r\n");
  if (!header_end)
    return 0; // Malformed request

  char header_section[MAX_BUFFER_SIZE];
  size_t header_len = header_end - client_state->in_buffer;
  strncpy(header_section, client_state->in_buffer, header_len);
  header_section[header_len] = '\0';

  char *request_line_end = strstr(header_section, "\r\n");
  if (request_line_end) {
    *request_line_end = '\0';
    sscanf(header_section, "%15s %1023s %15s", client_state->method,
           client_state->path, client_state->http_version);
    *request_line_end = '\r';
  }

  if (strcasecmp(client_state->method, "GET") != 0 &&
      strcasecmp(client_state->method, "POST") != 0) {
    create_http_error_response(client_state, 405, "Method Not Allowed");
    return 0;
  }

  parse_all_headers(client_state, header_section);

  const char *connection_header = get_header_value(client_state, "Connection");
  client_state->keep_alive =
      (connection_header && strcasecmp(connection_header, "keep-alive") == 0);

  client_state->content_length = 0;
  const char *content_length_header =
      get_header_value(client_state, "Content-Length");
  if (content_length_header) {
    client_state->content_length = atoi(content_length_header);
  }

  char *body_data_start = header_end + 4;
  size_t initial_body_data_len =
      client_state->in_buffer_len - (body_data_start - client_state->in_buffer);

  if (strcasecmp(client_state->method, "POST") == 0 &&
      client_state->content_length > 0) {
    if (client_state->body_buffer == NULL) {
      if (client_state->content_length > MAX_BODY_SIZE) {
        create_http_error_response(client_state, 413, "Payload Too Large");
        return 0;
      }
      client_state->body_buffer = malloc(client_state->content_length + 1);
      if (!client_state->body_buffer) {
        perror("malloc for body buffer failed");
        return 0;
      }
    }

    memcpy(client_state->body_buffer, body_data_start, initial_body_data_len);
    client_state->body_received = initial_body_data_len;
  }

  return 1;
}

// Function to prepare the HTTP response
void create_http_response(client_state_t *client_state) {
  const char *http_response_body = "<!DOCTYPE html>"
		                               "<html><body><h1>Hello World!</h1></body></html>";
  char http_headers[256];

  snprintf(http_headers, sizeof(http_headers),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: %zu\r\n",
           strlen(http_response_body));

  if (client_state->keep_alive) {
    strcat(http_headers, "Connection: keep-alive\r\n");
    char keep_alive_info[64];
    snprintf(keep_alive_info, sizeof(keep_alive_info),
             "Keep-Alive: timeout=%d, max=100\r\n",
             KEEPALIVE_IDLE_TIMEOUT_SECONDS);
    strcat(http_headers, keep_alive_info);
  } else {
    strcat(http_headers, "Connection: close\r\n");
  }

  snprintf(client_state->out_buffer, sizeof(client_state->out_buffer),
           "%s\r\n%s", http_headers, http_response_body);

  client_state->out_buffer_len = strlen(client_state->out_buffer);
  client_state->out_buffer_sent = 0;
}

void create_http_error_response(client_state_t *client_state, int status_code,
                                const char *message) {
  char status_line[128];
  const char *status_message;

  switch (status_code) {
  case 400:
    status_message = "Bad Request";
    break;
  case 404:
    status_message = "Not Found";
    break;
  case 405:
    status_message = "Method Not Allowed";
    break;
  case 500:
    status_message = "Internal Server Error";
    break;
  default:
    status_code = 500;
    status_message = "Internal Server Error";
    break;
  }

  snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", status_code,
           status_message);

  char http_headers[256];
  snprintf(http_headers, sizeof(http_headers),
           "Content-Type: text/plain\r\n"
           "Content-Length: %zu\r\n"
           "Connection: close\r\n",
           strlen(message));

  snprintf(client_state->out_buffer, sizeof(client_state->out_buffer),
           "%s%s\r\n%s", status_line, http_headers, message);

  client_state->out_buffer_len = strlen(client_state->out_buffer);
  client_state->out_buffer_sent = 0;
  client_state->keep_alive =
      0; // Error responses should generally close the connection
}
