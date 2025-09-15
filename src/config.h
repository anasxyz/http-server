#ifndef _CONFIG_H_
#define _CONFIG_H_

// forward declaration to allow for nested pointers
typedef struct route_config route_config;
typedef struct ssl_config ssl_config;
typedef struct server_config server_config;
typedef struct http_config http_config;
typedef struct config config;

extern config *global_config;

// represents a single route block within a server block
typedef struct route_config {
  char *uri;           // could be "/" or "/images" or whatever
  char *content_dir;   // overrides the default root directory in server block
  char **index_files;  // overrides the default index files in server block
  int num_index_files; // number of index files
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
} route_config;

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
  int listen_port;       // port to listen on
  char **server_names;   // array of server names
  int num_server_names;  // number of server names
  char *content_dir;     // default root dir for all routes
  char **index_files;    // default index files for all routes
  int num_index_files;   // number of index files
  ssl_config *ssl;       // ssl config
  route_config *routes;  // array of routes
  int num_routes;        // number of routes
  int return_status;     // server-level redirects
  char *return_url_text; // url server-level redirects

  char *access_log_path; // overrides the default access log path in http block
  char *error_log_path;  // overrides the default error log path in http block
  char *log_format;

  long timeout; // timeout for idle connections in seconds
} server_config;

typedef struct http_config {
  long default_buffer_size; // default buffer size for everything if not
                            // overridden
  long body_buffer_size;    // default buffer size for response body data
  long headers_buffer_size; // default buffer size for response headers

  char *mime_types_path; // path to mime types file
  char *default_type;    // default MIME type when one isn't found
  char *access_log_path; // path to access log file
  char *error_log_path;  // path to error log file
  char *log_format;      // log format string
  int sendfile;          // 0 for off, 1 for on for sendfile()

  server_config *servers; // array of servers in http block
  int num_servers;        // number of servers
} http_config;

// top-level config struct for entire configuration
typedef struct config {
  int max_connections; // max number of connections
  int worker_processes; // number of worker processes
  char *user; // user to run as
  char *pid_file; // path to pid file
  char *log_file; // path to log file

  http_config *http; // http block config
} config;

/**
 * @brief initialises the global config struct.
 */
void init_config();

/**
 * @brief parses a configuration file and populates the global config struct.
 * @param config_file_path the path to the configuration file.
 * @return 0 on success, -1 on failure.
 */
int parse_config(char *config_file_path);

/**
 * @brief loads a configuration file and populates the global config struct.
 * @param config_file_path the path to the configuration file.
 */
void load_config(char *config_file_path);

/**
 * @brief parses a duration string in milliseconds.
 * @param str the string to parse.
 * @return the duration in milliseconds, or -1 on failure.
 */
long parse_duration_ms(const char *str);

/**
 * @brief parses a buffer size string in bytes.
 * @param str the string to parse.
 * @return the buffer size in bytes, or -1 on failure.
 */
long parse_buffer_size(const char *str);

/**
 * @brief frees the global config struct.
 */
void free_config();

/**
 * @brief trims whitespace from the start and end of a string.
 * @param str the string to trim.
 * @return a pointer to the trimmed string.
 */
char *trim(char *str);

#endif // _CONFIG_H_
