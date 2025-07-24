#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h> // for bool type
#include <errno.h>   // for errno

#include "../include/config.h"      // now includes the Config struct and accessors
#include "../include/proxy.h"
#include "../include/utils_path.h" // for join_paths

#define DEFAULT_HTTP_PORT 80

// parses the proxy->to url into host and port.
// returns 0 on success or -1 on failure.
int parse_proxy_target(const char *url, char *host, size_t host_size,
                       int *port) {
    if (!url || !host || !port)
        return -1;

    const char *p = url;

    // skip http:// or https:// scheme if present
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        // treat https as http (no tls) for now
    }

    // now p points at host[:port][/...]

    // find end of host (':' or '/' or just end)
    size_t i = 0;
    while (p[i] && p[i] != ':' && p[i] != '/')
        i++;

    if (i >= host_size) // ensure we don't overflow the host buffer
        i = host_size - 1;
    strncpy(host, p, i);
    host[i] = '\0';

    *port = DEFAULT_HTTP_PORT; // default

    // if next char is ':' then parse port
    if (p[i] == ':') {
        i++; // move past ':'
        // parse port digits until '/' or end
        int j = i;
        while (p[j] && isdigit((unsigned char)p[j]))
            j++;

        char port_str[6] = {0}; // max 65535 + null
        size_t len = j - i;
        if (len >= sizeof(port_str))
            return -1; // port too long

        strncpy(port_str, p + i, len);
        *port = atoi(port_str);
        if (*port <= 0 || *port > 65535) // basic port validation
            return -1; // invalid port
    }

    return 0;
}

// this function finds a matching proxy rule for the given request path
// it uses the globally active configuration
Proxy *find_proxy_for_path(const char *request_path) {
    // get the current active configuration
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        fprintf(stderr, "error: no active configuration found for proxy lookup!\n");
        return NULL;
    }

    for (size_t i = 0; i < current_cfg->proxies_count; i++) { // use current_cfg->proxies_count
        size_t prefix_len = strlen(current_cfg->proxies[i].from); // use current_cfg->proxies[i].from

        // if proxy->from ends with '/' then remove it for matching
        bool ends_with_slash =
            (prefix_len > 0 && current_cfg->proxies[i].from[prefix_len - 1] == '/'); // use current_cfg->proxies[i].from

        if (ends_with_slash) {
            // match with and without trailing slash
            // case 1: request_path starts exactly with current_cfg->proxies[i].from
            if (strncmp(request_path, current_cfg->proxies[i].from, prefix_len) == 0)
                return &current_cfg->proxies[i]; // return pointer to the specific proxy in the config struct

            // case 2: match request_path == proxy prefix without trailing slash
            if (prefix_len > 0 && strncmp(request_path, current_cfg->proxies[i].from, prefix_len - 1) == 0 &&
                (request_path[prefix_len - 1] == '\0' ||
                 request_path[prefix_len - 1] == '/'))
                return &current_cfg->proxies[i]; // return pointer to the specific proxy
        } else {
            // if proxy->from does not end with '/' just match prefix normally
            if (strncmp(request_path, current_cfg->proxies[i].from, prefix_len) == 0)
                return &current_cfg->proxies[i]; // return pointer to the specific proxy
        }
    }
    return NULL;
}

// returns a new mallocd string with the path prefix stripped
char *strip_prefix(const char *path, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) == 0) {
        // if the prefix matches, return strdup of the suffix
        return strdup(path + prefix_len);
    }
    return strdup(path); // no prefix match so just return original path copy
}

// this function handles a blocking proxy request
// it connects to the backend, sends the modified request, and reads the response
ProxyResult *proxy_request(Proxy *proxy, const char *original_request_str) {
    char host[256];
    int port = 0;

    if (parse_proxy_target(proxy->to, host, sizeof(host), &port) != 0) {
        fprintf(stderr, "failed to parse proxy target: %s\n", proxy->to);
        return NULL;
    }

    printf("proxy forwarding to host: %s port: %d\n", host, port);

    const char *first_line_end = strstr(original_request_str, "\r\n");
    if (!first_line_end) return NULL;

    size_t first_line_len = first_line_end - original_request_str;
    char *first_line = strndup(original_request_str, first_line_len);
    if (!first_line) return NULL;

    char method[16], path[1024], version[16];
    // use %n to get the number of characters consumed, to correctly extract method
    int consumed_len;
    if (sscanf(first_line, "%15s %1023s %15s%n", method, path, version, &consumed_len) != 3) {
        fprintf(stderr, "failed to parse request line: %s\n", first_line);
        free(first_line);
        return NULL;
    }
    free(first_line);

    char *suffix = strip_prefix(path, proxy->from);
    if (!suffix) return NULL;

    const char *base_path = "/";
    const char *scheme_pos = strstr(proxy->to, "://");
    if (scheme_pos) {
        // find the first '/' after the scheme+host part
        const char *host_end = scheme_pos + 3; // start after "http://" or "https://"
        const char *temp_p = host_end;
        while (*temp_p && *temp_p != '/' && *temp_p != ':') { // find end of host or start of port
            temp_p++;
        }
        if (*temp_p == ':') { // if there's a port, skip it
            while (*temp_p && *temp_p != '/') {
                temp_p++;
            }
        }
        base_path = (*temp_p != '\0') ? temp_p : "/"; // base_path is the part after host/port
    }

    char *full_path = join_paths(base_path, suffix);
    free(suffix);

    // ensure full_path starts with a '/'
    if (full_path && full_path[0] != '/') {
        char *fixed = malloc(strlen(full_path) + 2);
        if (!fixed) {
            free(full_path);
            return NULL;
        }
        fixed[0] = '/';
        strcpy(fixed + 1, full_path);
        free(full_path);
        full_path = fixed;
    } else if (!full_path) { // handle case where join_paths might return null
        return NULL;
    }


    char new_request_line[1152]; // sufficient size for method path http/1.1\r\n
    snprintf(new_request_line, sizeof(new_request_line), "%s %s %s\r\n", method, full_path, version);
    free(full_path);

    const char *rest = first_line_end + 2; // move past the first \r\n
    size_t new_len = strlen(new_request_line) + strlen(rest) + 1;
    char *modified_request_str = malloc(new_len);
    if (!modified_request_str) return NULL;

    strcpy(modified_request_str, new_request_line);
    strcat(modified_request_str, rest);

    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        perror("getaddrinfo failed"); // added perror for more details
        free(modified_request_str);
        return NULL;
    }

    int sockfd = -1;
    for (p = res; p; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);
    if (sockfd == -1) {
        fprintf(stderr, "failed to connect to proxy target.\n"); // more specific error
        free(modified_request_str);
        return NULL;
    }

    size_t len = strlen(modified_request_str);
    ssize_t sent = 0;
    while (sent < (ssize_t)len) {
        ssize_t n = send(sockfd, modified_request_str + sent, len - sent, 0);
        if (n <= 0) {
            perror("send to proxy failed"); // added perror
            close(sockfd);
            free(modified_request_str);
            return NULL;
        }
        sent += n;
    }
    free(modified_request_str);

    size_t bufsize = 8192;
    size_t offset = 0;
    char *buffer = malloc(bufsize);
    if (!buffer) {
        perror("malloc for proxy response buffer failed");
        close(sockfd);
        return NULL;
    }

    ssize_t nread;
    // loop to read all available data from the proxy, reallocating buffer if needed
    while ((nread = recv(sockfd, buffer + offset, bufsize - offset - 1, 0)) > 0) { // -1 for null terminator
        offset += nread;
        if (offset + 1024 >= bufsize) { // check if we need more space soon
            bufsize *= 2;
            char *tmp = realloc(buffer, bufsize);
            if (!tmp) {
                perror("realloc for proxy response buffer failed");
                free(buffer);
                close(sockfd);
                return NULL;
            }
            buffer = tmp;
        }
    }

    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // no more data for now, but not necessarily an error if connection is active
            // for a blocking proxy request, this generally means connection closed or no more data.
            // if it were non-blocking, we'd add to epoll.
        } else {
            perror("recv from proxy failed"); // added perror
            free(buffer);
            close(sockfd);
            return NULL;
        }
    }

    close(sockfd);
    buffer[offset] = '\0'; // null-terminate the received data

    // --- split headers and body ---
    const char *header_end = strstr(buffer, "\r\n\r\n");
    if (!header_end) {
        fprintf(stderr, "proxy response malformed: no double crlf found.\n");
        free(buffer);
        return NULL;
    }

    size_t header_len = header_end - buffer + 4; // include the \r\n\r\n
    char *headers = malloc(header_len + 1);
    if (!headers) {
        perror("malloc for proxy headers failed");
        free(buffer);
        return NULL;
    }
    memcpy(headers, buffer, header_len);
    headers[header_len] = '\0';

    size_t body_len = offset - header_len;
    char *body = NULL; // initialize to null

    if (body_len > 0) {
        body = malloc(body_len + 1);
        if (!body) {
            perror("malloc for proxy body failed");
            free(headers);
            free(buffer);
            return NULL;
        }
        memcpy(body, buffer + header_len, body_len);
        body[body_len] = '\0';
    }

    free(buffer);

    ProxyResult *result = malloc(sizeof(ProxyResult));
    if (!result) {
        perror("malloc for ProxyResult failed");
        free(headers);
        if (body) free(body);
        return NULL;
    }

    result->headers = headers;
    result->body = body; // will be null if body_len was 0
    return result;
}
