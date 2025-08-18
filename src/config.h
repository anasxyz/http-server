#ifndef _CONFIG_H_
#define _CONFIG_H_

// forward declaration to allow for nested pointers
typedef struct location_config location_config;
typedef struct ssl_config ssl_config;
typedef struct server_config server_config;
typedef struct http_config http_config;
typedef struct config config;

extern config *global_config;

// represents a single location block within a server block
typedef struct location_config {
  char *uri;           // could be "/" or "/images" or whatever
  char *content_dir;      // overrides the default root directory in server block
  char **index_files;  // overrides the default index files in server block
	int num_index_files;  // number of index files
  char *proxy_url;     // for reverse proxying
  int autoindex;       // 0 for off, 1 for on
  char **allowed_ips;  // array of allowed ips
  int num_allowed_ips; // number of allowed ips
  char **denied_ips;   // array of denied ips
  int num_denied_ips;  // number of denied ips
  int return_status; // overrides the default return status code in server block
  char *return_url_text; // overrides the default return text in server block

  char *etag_header;
  char *expires_header;
} location_config;

// represents the ssl config for a server block
typedef struct ssl_config {
  char *cert_file;   // path to certificate file
  char *key_file;    // path to key file
  char **protocols;  // array of protocols
  int num_protocols; // number of protocols
  char **ciphers;    // array of ciphers
  int num_ciphers;   // number of ciphers
} ssl_config;

// represents a single server block or virtual host
typedef struct server_config {
  int listen_port;            // port to listen on
  char **server_names;          // array of server names
	int num_server_names;          // number of server names
  char *content_dir;             // default root dir for all locations
  char **index_files;         // default index files for all locations
	int num_index_files;         // number of index files
  ssl_config *ssl;            // ssl config
  location_config *locations; // array of locations
  int num_locations;          // number of locations
  int return_status;          // server-level redirects
  char *return_url_text;      // url server-level redirects

  char *access_log_path; // overrides the default access log path in http block
  char *error_log_path;  // overrides the default error log path in http block
  char *log_format;

  long timeout; // timeout for idle connections in seconds
} server_config;

typedef struct http_config {
  char *mime_types_path; // path to mime types file
  char *default_type;    // default MIME type when one isn't found
  char *access_log_path;
  char *error_log_path;
  char *log_format;

  server_config *servers;
  int num_servers;
} http_config;

// top-level config struct for entire configuration
typedef struct config {
	int max_connections;
  int worker_processes;
  char *user;
  char *pid_file;
  char *log_file;
  http_config *http;
} config;

void init_config();
void parse_config();
long parse_duration_ms(const char *str);
void load_config();
void free_config();
char *trim(char *str);

#endif // _CONFIG_H_
