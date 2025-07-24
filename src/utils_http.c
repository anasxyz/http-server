#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // for stat, fstat
#include <time.h>
#include <unistd.h>

#include "../include/config.h"      // now includes the Config struct and accessors
#include "../include/utils_file.h"  // for is_dir
#include "../include/utils_http.h"
#include "../include/utils_path.h"  // for join_paths

#define MAX_HEADERS 100
#define MAX_PATH 1024

// tries to resolve a request path based on 'try_files' rules from the configuration
char *try_paths(const char *request_path) {
    if (!request_path)
        return NULL;

    char *resolved = NULL;

    // get the current active configuration
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        fprintf(stderr, "error: no active configuration found for try_paths!\n");
        return NULL;
    }

    for (size_t i = 0; i < current_cfg->try_files_count; i++) { // use current_cfg->try_files_count
        const char *rule = current_cfg->try_files[i]; // use current_cfg->try_files

        // --- case 1: $uri ---
        if (strcmp(rule, "$uri") == 0) {
            printf("trying $uri: %s\n", request_path);
            resolved = realpath(request_path, NULL);
            // ensure it's a file, not a directory
            if (resolved && !is_dir(resolved)) { // check if resolved path is a directory
                char *result = strdup(resolved);
                free(resolved);
                return result;
            }
            free(resolved);
            resolved = NULL; // reset for next iteration
        }

        // --- case 2: $uri/ ---
        else if (strcmp(rule, "$uri/") == 0) {
            // only apply if the request path is a directory or points to a directory
            char *dir_path_resolved = realpath(request_path, NULL);
            if (dir_path_resolved && is_dir(dir_path_resolved)) {
                printf("trying $uri/ for directory: %s\n", dir_path_resolved);
                for (size_t j = 0; j < current_cfg->index_files_count; j++) { // use current_cfg->index_files_count
                    char *candidate = join_paths(dir_path_resolved, current_cfg->index_files[j]); // use current_cfg->index_files
                    printf("trying $uri/ index: %s\n", candidate);
                    resolved = realpath(candidate, NULL);
                    if (resolved && !is_dir(resolved)) { // ensure it's a file
                        char *result = strdup(resolved);
                        free(resolved);
                        free(candidate);
                        free(dir_path_resolved);
                        return result;
                    }
                    free(resolved);
                    free(candidate);
                    resolved = NULL;
                }
            }
            free(dir_path_resolved);
        }

        // --- case 3: fallback file (e.g., /fallback.html) ---
        else {
            char *fallback_path_candidate = NULL;

            // if fallback rule starts with '/', join it with root (strip leading '/')
            if (rule[0] == '/') {
                // remove leading '/' if root also implies it
                fallback_path_candidate = join_paths(current_cfg->root, rule + 1); // use current_cfg->root
            } else {
                fallback_path_candidate = join_paths(current_cfg->root, rule); // use current_cfg->root
            }

            printf("trying fallback file with root: %s\n", fallback_path_candidate);
            resolved = realpath(fallback_path_candidate, NULL);
            free(fallback_path_candidate);

            if (resolved && !is_dir(resolved)) { // ensure it's a file
                char *result = strdup(resolved);
                free(resolved);
                return result;
            }
            free(resolved);
            resolved = NULL;
        }
    }

    return NULL;
}

// checks if the request path matches any defined aliases in the configuration
char *check_for_alias_match(const char *request_path) {
    printf("resolving request path: %s\n", request_path);

    // get the current active configuration
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        fprintf(stderr, "error: no active configuration found for alias lookup!\n");
        // fallback to default root directly if config is missing
        char *fallback_path = join_paths(NULL, request_path); // join with NULL, effectively strdup
        if (!fallback_path) {
            fprintf(stderr, "error: malloc failed in check_for_alias_match fallback.\n");
        }
        return fallback_path; // this will likely create a path relative to current working dir, which might not be desired
    }


    for (size_t i = 0; i < current_cfg->aliases_count; i++) { // use current_cfg->aliases_count
        const char *prefix = current_cfg->aliases[i].from; // use current_cfg->aliases[i]
        const char *mapped_path = current_cfg->aliases[i].to; // use current_cfg->aliases[i]

        printf("checking alias: \"%s\" -> \"%s\"\n", prefix, mapped_path);

        if (strncmp(request_path, prefix, strlen(prefix)) == 0) {
            const char *suffix = request_path + strlen(prefix);
            printf("alias match found! replacing \"%s\" with \"%s\"\n", prefix,
                   mapped_path);
            printf("suffix after prefix: \"%s\"\n", suffix);

            // check if mapped_path is file
            // realpath here is important to resolve symbolic links and get absolute path
            char *resolved_mapped_path = realpath(mapped_path, NULL);
            if (!resolved_mapped_path) {
                perror("realpath for alias mapped_path failed");
                // if mapped_path doesn't exist, this alias is effectively broken for now
                // or we could decide to treat it as a directory to join with suffix
                resolved_mapped_path = strdup(mapped_path); // fallback to just a copy
                if (!resolved_mapped_path) return NULL;
            }


            if (!is_dir(resolved_mapped_path)) {
                printf("alias target is a file. using it directly: %s\n", resolved_mapped_path);
                // resolved_mapped_path is already malloc'd by realpath or strdup
                return resolved_mapped_path;
            }

            // if directory, join suffix
            char *final_path = join_paths(resolved_mapped_path, suffix);
            free(resolved_mapped_path); // free the temporary resolved path
            printf("resolved path using alias: %s\n", final_path);
            return final_path;
        }
    }

    // no alias matched so fall back to default root
    printf("no alias matched. falling back to root: %s\n", current_cfg->root); // use current_cfg->root
    char *fallback_path = join_paths(current_cfg->root, request_path); // use current_cfg->root
    printf("resolved path using root: %s\n", fallback_path);

    return fallback_path;
}

// returns the http status reason string for a given status code
char *get_status_reason(int code) {
    switch (code) {
    case 100:
        return "continue";
    case 101:
        return "switching protocols";
    case 102:
        return "processing";
    case 200:
        return "ok";
    case 201:
        return "created";
    case 202:
        return "accepted";
    case 203:
        return "non-authoritative information";
    case 204:
        return "no content";
    case 205:
        return "reset content";
    case 206:
        return "partial content";
    case 300:
        return "multiple choices";
    case 301:
        return "moved permanently";
    case 302:
        return "found";
    case 303:
        return "see other";
    case 304:
        return "not modified";
    case 307:
        return "temporary redirect";
    case 308:
        return "permanent redirect";
    case 400:
        return "bad request";
    case 401:
        return "unauthorized";
    case 402:
        return "payment required";
    case 403:
        return "forbidden";
    case 404:
        return "not found";
    case 405:
        return "method not allowed";
    case 406:
        return "not acceptable";
    case 408:
        return "request timeout";
    case 409:
        return "conflict";
    case 410:
        return "gone";
    case 411:
        return "length required";
    case 413:
        return "payload too large";
    case 414:
        return "uri too long";
    case 415:
        return "unsupported media type";
    case 426:
        return "upgrade required";
    case 429:
        return "too many requests";
    case 500:
        return "internal server error";
    case 501:
        return "not implemented";
    case 502:
        return "bad gateway";
    case 503:
        return "service unavailable";
    case 504:
        return "gateway timeout";
    case 505:
        return "http version not supported";
    default:
        return "unknown status";
    }
}

// generates the current http date string
char *http_date_now() {
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char *buf = malloc(30);
    if (!buf)
        return NULL;
    strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT", tm);
    return buf;
}

// generates the http last-modified date string for a given file path
char *http_last_modified(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0)
        return NULL;

    struct tm *tm = gmtime(&st.st_mtime);
    char *buf = malloc(30);
    if (!buf)
        return NULL;
    strftime(buf, 30, "%a, %d %b %Y %H:%M:%S GMT", tm);
    return buf;
}

// helper to trim trailing crlf and spaces
void trim_crlf(char *line) {
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       isspace(line[len - 1]))) {
        line[len - 1] = '\0';
        len--;
    }
}

// parses a raw http request string into an HttpRequest struct
// caller must free the returned HttpRequest struct
HttpRequest *parse_request(const char *raw_request) {
    if (!raw_request)
        return NULL;

    HttpRequest *req = malloc(sizeof(HttpRequest));
    if (!req)
        return NULL;
    memset(req, 0, sizeof(HttpRequest));

    // we'll copy raw_request so we can tokenize it safely
    char *buffer = strdup(raw_request);
    if (!buffer) {
        free(req);
        return NULL;
    }

    char *line = buffer;
    char *next_line;

    // parse request line (first line)
    next_line = strstr(line, "\r\n");
    if (!next_line) {
        // maybe just '\n'
        next_line = strchr(line, '\n');
        if (!next_line)
            goto error;
    }
    *next_line = '\0';

    // tokenize request line: method path version
    char *method = strtok(line, " ");
    char *path = strtok(NULL, " ");
    char *version = strtok(NULL, " ");
    if (!method || !path || !version)
        goto error;

    req->request_line.method = strdup(method);
    req->request_line.path = strdup(path);
    req->request_line.version = strdup(version);

    if (!req->request_line.method || !req->request_line.path ||
        !req->request_line.version)
        goto error;

    // move to next line (headers start here)
    line = next_line + 2; // skip \r\n
    if (*line == '\n')
        line++; // handle \r\n or \n

    // parse headers
    req->headers = malloc(sizeof(Header) * MAX_HEADERS);
    if (!req->headers)
        goto error;
    req->header_count = 0;

    while (*line && req->header_count < MAX_HEADERS) {
        next_line = strstr(line, "\r\n");
        if (!next_line)
            next_line = strchr(line, '\n');
        if (!next_line)
            break; // no more complete lines

        *next_line = '\0';
        trim_crlf(line);

        // empty line means end of headers
        if (strlen(line) == 0)
            break;

        // split header into key and value by ':'
        char *colon = strchr(line, ':');
        if (!colon)
            goto error; // malformed header

        *colon = '\0';
        char *key = line;
        char *value = colon + 1;

        // trim spaces from value
        while (*value == ' ')
            value++;

        req->headers[req->header_count].key = strdup(key);
        req->headers[req->header_count].value = strdup(value);
        if (!req->headers[req->header_count].key ||
            !req->headers[req->header_count].value)
            goto error;

        req->header_count++;

        line = next_line + 2;
        if (*line == '\n')
            line++; // handle \r\n or \n
    }

    free(buffer);
    return req;

error:
    if (buffer)
        free(buffer);
    if (req) {
        // free partially allocated fields
        if (req->request_line.method)
            free(req->request_line.method);
        if (req->request_line.path)
            free(req->request_line.path);
        if (req->request_line.version)
            free(req->request_line.version);
        for (size_t i = 0; i < req->header_count; i++) {
            if (req->headers[i].key) free(req->headers[i].key);
            if (req->headers[i].value) free(req->headers[i].value);
        }
        if (req->headers)
            free(req->headers);
        free(req);
    }
    return NULL;
}

// sets or updates a header in the http response
void set_header(HttpResponse *res, char *key, char *val) {
    for (size_t i = 0; i < res->header_count; i++) {
        if (strcmp(res->headers[i].key, key) == 0) {
            free(res->headers[i].value);
            res->headers[i].value = strdup(val);
            return;
        }
    }
    // if header not found, this function currently doesn't add it.
    // you might want to extend it to add new headers if needed.
}

// frees all dynamically allocated memory within an HttpResponse struct
void free_response(HttpResponse *response) {
    if (!response)
        return;

    // free status line strings
    if (response->status_line.http_version) {
        free(response->status_line.http_version);
    }
    if (response->status_line.status_reason) {
        free(response->status_line.status_reason);
    }

    // free each header key and value
    for (size_t i = 0; i < response->header_count; i++) {
        if (response->headers[i].key) {
            free(response->headers[i].key);
        }
        if (response->headers[i].value) {
            free(response->headers[i].value);
        }
    }

    // free headers array itself
    if (response->headers) {
        free(response->headers);
    }

    // if file_fd is open, close it (only for success responses that open files)
    if (response->file_fd != -1) {
        close(response->file_fd);
    }

    free(response);
}

// frees all dynamically allocated memory within an HttpRequest struct
void free_request(HttpRequest *request) {
    if (!request)
        return;

    // free request line strings
    if (request->request_line.method) {
        free(request->request_line.method);
    }
    if (request->request_line.path) {
        free(request->request_line.path);
    }
    if (request->request_line.version) {
        free(request->request_line.version);
    }

    // free each header key and value
    for (size_t i = 0; i < request->header_count; i++) {
        if (request->headers[i].key) {
            free(request->headers[i].key);
        }
        if (request->headers[i].value) {
            free(request->headers[i].value);
        }
    }

    // free headers array itself
    if (request->headers) {
        free(request->headers);
    }

    // finally free the HttpRequest struct itself
    free(request);
}
