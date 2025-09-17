#ifndef DEFAULTS_H
#define DEFAULTS_H

#define NAME "http-server"
#define VERSION "0.5.1"

#define DEFAULT_CONFIG_PATH "/etc/http-server/http-server.conf"

// TODO: dont hardcode app name
#define DEFAULT_WORKER_PROCESSES 4
#define DEFAULT_MAX_CONNECTIONS 1000
#define DEFAULT_USER "www-data"
#define DEFAULT_PID_FILE "/var/run/http-server.pid"
#define DEFAULT_LOG_FILE "/var/log/http-server/http-server.log"

#define DEFAULT_DEFAULT_BUFFER_SIZE (4 * 1024)
#define DEFAULT_BODY_BUFFER_SIZE (4 * 1024)
#define DEFAULT_HEADERS_BUFFER_SIZE 1024

#define DEFAULT_MIME_PATH "/etc/http-server/mime.types"
#define DEFAULT_DEFAULT_TYPE "text/plain"
#define DEFAULT_ACCESS_LOG "/var/log/http-server/access.log"
#define DEFAULT_ERROR_LOG "/var/log/http-server/error.log"
#define DEFAULT_LOG_FORMAT "combined"
#define DEFAULT_SENDFILE 1

#endif // DEFAULTS_H
