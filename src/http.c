#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <time.h>
#include <errno.h>

#include "../include/config.h"
#include "../include/http.h"
#include "../include/proxy.h"
#include "../include/utils_http.h"
#include "../include/utils_path.h"

#define MAX_BUFFER_SIZE 8192

Header headers[] = {{"Date", ""},
                    {"Server", ""},
                    {"Last-Modified", ""},
                    {"Content-Length", ""},
                    {"Content-Type", ""},
                    {"Connection", ""}};

MethodHandler handlers[] = {
    {"GET", handle_get},
};

HttpResponse *create_response(int status_code, char *path) {
    HttpResponse *response = malloc(sizeof(HttpResponse));
    if (!response) {
        // log: malloc failure
        log_error("malloc for httpresponse failed");
        return NULL;
    }

    // init response fields
    response->file_fd = -1; // -1 just because no file is open by default
    response->file_size = 0;
    response->status_line.http_version = NULL;
    response->status_line.status_reason = NULL;
    response->headers = NULL;
    response->header_count = 0;

    char* content_type = NULL;

    // Get the current active configuration
    // This is crucial: all config-dependent logic now uses `current_cfg`
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        // log: critical config error
        log_error("no active configuration found when creating response!");
        fprintf(stderr, "Error: No active configuration found when creating response!\n");
        free_response(response); // Free partially allocated response
        return NULL; // Critical error
    }

    // set status line
    response->status_line.http_version = strdup("HTTP/1.1");
    response->status_line.status_code = status_code;
    response->status_line.status_reason =
        strdup(get_status_reason(response->status_line.status_code));

    size_t header_count = sizeof(headers) / sizeof(Header);
    response->headers = malloc(sizeof(Header) * header_count);
    if (!response->headers) {
        // log: malloc failure
        log_error("malloc for response headers failed in create_response");
        free_response(response);
        return NULL;
    }
    response->header_count = header_count;
    for (size_t i = 0; i < header_count; i++) {
        response->headers[i].key = strdup(headers[i].key);
        response->headers[i].value = strdup(headers[i].value); // init values as empty strings
    }

    // if error
    if (status_code >= 400) { // for errors, path might be NULL or irrelevant
        char error_file_path[512];
        // Use current_cfg->root here
        snprintf(error_file_path, sizeof(error_file_path), "%s/%d.html", current_cfg->root, status_code);

        response->file_fd = open(error_file_path, O_RDONLY | O_NONBLOCK); // open non blocking
        if (response->file_fd == -1) {
            // log: error opening error file
            log_error("error opening error file %s for status %d: %s", error_file_path, status_code, strerror(errno));
            perror("Error opening error file");
            // here i should probably fallback to generic error message or close connection
            // but for now i'll just make sure content_type and size are handled
            set_header(response, "Content-Type", "text/plain");
            set_header(response, "Content-Length", "0"); // no body
            // Ensure file_fd is -1 if open failed, it should be already set
            return response; // Return response with no body
        }

        struct stat st;
        if (fstat(response->file_fd, &st) == -1) {
            // log: fstat error
            log_error("fstat error file %s failed: %s", error_file_path, strerror(errno));
            perror("fstat error file");
            close(response->file_fd);
            response->file_fd = -1;
            free_response(response); // clean up if fstat fails
            return NULL;
        }
        response->file_size = st.st_size;
        content_type = get_mime_type(error_file_path);
    } else {
        // for successful responses the path should always be provided
        if (!path) { // should not happen for 2xx responses normally
            // log: null path for 2xx response
            log_error("path is null for successful (2xx) response creation. this should not happen.");
            free_response(response);
            return create_response(500, NULL); // internal Server Error
        }
        // open file
        response->file_fd = open(path, O_RDONLY | O_NONBLOCK); // open non blocking
        if (response->file_fd == -1) {
            // log: error opening requested file
            log_error("error opening requested file %s: %s", path, strerror(errno));
            perror("Error opening requested file");
            // could be a 404 if logic failed to catch it or a 403 for pwermissions
            free_response(response);
            return create_response(404, NULL); // Or 403 depending on errno
        }

        // get file size
        struct stat st;
        if (fstat(response->file_fd, &st) == -1) {
            // log: fstat error
            log_error("fstat requested file %s failed: %s", path, strerror(errno));
            perror("fstat requested file");
            close(response->file_fd);
            response->file_fd = -1;
            free_response(response);
            return NULL;
        }
        response->file_size = st.st_size;

        content_type = get_mime_type(path);
    }

    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", response->file_size);

    char* date = http_date_now();
    // this line needs to be fixed to get actual file last modified. using http_date_now() for simplicity.
    char *last_modified_str = (path && response->file_fd != -1) ? http_last_modified(path) : strdup("");


    set_header(response, "Server", "http-server");
    set_header(response, "Date", date);
    set_header(response, "Connection", "close"); // start with close persistent can be added later
    set_header(response, "Content-Type", content_type ? content_type : "application/octet-stream"); // default if mime type is null
    set_header(response, "Content-Length", content_length_str);
    if (last_modified_str) {
        set_header(response, "Last-Modified", last_modified_str);
    } else {
        set_header(response, "Last-Modified", ""); // ensure it's not null
    }

    free(date);
    if (last_modified_str) free(last_modified_str); // free if strdup was used

    return response;
}

// serialise response to string.
// this function will now return a dynamically allocated string containing the headers
// i changed it so that it will NOT include the body, the file will be sent separately with sendfile()
char *serialise_response(HttpResponse *response) {
    // here i just estimate the size so just max headers + status line + CRLF
    int size = 1024 + (response->header_count * 128);
    char *buffer = malloc(size);
    if (!buffer) {
        // log: malloc failure
        log_error("malloc for serialise_response buffer failed");
        return NULL;
    }
    buffer[0] = '\0'; // ensure null-termination for snprintf

    int offset = snprintf(
        buffer, size, "%s %d %s\r\n", response->status_line.http_version,
        response->status_line.status_code, response->status_line.status_reason);

    if (offset < 0 || offset >= size) { // check for snprintf error or truncation
        log_error("snprintf error or truncation for status line in serialise_response");
        free(buffer);
        return NULL;
    }

    for (size_t i = 0; i < response->header_count; i++) {
        int written = snprintf(buffer + offset, size - offset, "%s: %s\r\n",
                               response->headers[i].key, response->headers[i].value);
        if (written < 0 || written >= size - offset) { // check for snprintf error or truncation
            log_error("snprintf error or truncation for header %s in serialise_response", response->headers[i].key);
            free(buffer);
            return NULL;
        }
        offset += written;
    }

    int written = snprintf(buffer + offset, size - offset, "\r\n");
    if (written < 0 || written >= size - offset) { // check for snprintf error or truncation
        log_error("snprintf error or truncation for final crlf in serialise_response");
        free(buffer);
        return NULL;
    }
    offset += written;

    return buffer;
}

// Method handler for GET requests. Uses current config for path resolution.
HttpResponse *handle_get(HttpRequest *request) {
    char *path = NULL;
    char *resolved_path = NULL;
    HttpResponse *response = NULL;

    // --- check for alias matches ---
    // check_for_alias_match now internally uses get_current_config()
    path = check_for_alias_match(request->request_line.path);
    if (!path) { // check_for_alias_match can return NULL on malloc fail
        // log: alias match failure
        log_error("check_for_alias_match failed (likely malloc issue) for path: %s", request->request_line.path);
        return create_response(500, NULL);
    }

    // --- try files from config ---
    // try_paths now internally uses get_current_config()
    resolved_path = try_paths(path);

    if (!resolved_path) {
        // path doesn't exist - page not found
        // log: file not found
        log_access("file not found for path: %s", path);
        response = create_response(404, NULL);
        goto end; // goto for cleanup before returning
    }

    // if path exists
    response = create_response(200, resolved_path);
    if (!response) { // If create_response failed for 200 OK (e.g., file open error)
        // log: create_response failed for 200 OK
        log_error("create_response failed for 200 OK with path: %s", resolved_path);
        response = create_response(500, NULL); // Fallback to Internal Server Error
    }

end:
    if (path) free(path);
    if (resolved_path) free(resolved_path);
    return response;
}

// this is the new main entry point from main.c for handling client requests
// it just processes the request and prepares the response but does NOT send it compared to before
// this is the non blocking version
void handle_request(ClientState *client_state) {
    HttpRequest *request = NULL;
    HttpResponse *response = NULL;
    ProxyResult *proxy_result = NULL;
    char *client_ip_str = NULL; // to get client ip for logging

    // get client ip for logging
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (getpeername(client_state->fd, (struct sockaddr *)&client_addr, &addr_len) == 0) {
        client_ip_str = inet_ntoa(client_addr.sin_addr);
    } else {
        log_error("getpeername failed for client fd %d: %s", client_state->fd, strerror(errno));
        client_ip_str = "unknown";
    }

    // Get the current active configuration
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        // log: critical config error
        log_error("no active configuration found for request handling for client %s!", client_ip_str);
        fprintf(stderr, "Error: No active configuration found for request handling!\n");
        response = create_response(500, NULL); // Internal Server Error
        goto prepare_response_headers;
    }

    // parse the request from the accumulated read_buffer
    request = parse_request(client_state->read_buffer);
    client_state->request = request; // store for later freeing

    if (!request) {
        // log: bad request
        log_error("failed to parse request from client %s. raw buffer: \"%s\"", client_ip_str, client_state->read_buffer);
        response = create_response(400, NULL); // bad request
        goto prepare_response_headers;
    }

    // --- proxy handling ---
    // find_proxy_for_path now internally uses current_cfg->proxies
    Proxy *proxy = find_proxy_for_path(request->request_line.path);
    if (proxy) {
        // i want to note here that proxy_request would also need to be non blocking or spawn a separate
        // worker if it involves network I/O but for now i'll just assume that it's synchronous
        // or handled by a separate epoll instance for proxy connections
        // the fturue plan is likely to register the proxy target
        // socket with epoll too and handle its events.
        proxy_result = proxy_request(proxy, client_state->read_buffer); // reuse raw request buffer
        if (!proxy_result) {
            // log: proxy request failed
            log_error("proxy request to %s failed for client %s (path: %s): %s", proxy->to, client_ip_str, request->request_line.path, strerror(errno));
            response = create_response(502, NULL); // bad gateway
            goto prepare_response_headers;
        }

        // for proxied responses i bypass standard HttpResponse creation and directly
        // use proxy_result's headers and body
        client_state->response_headers_buffer = strdup(proxy_result->headers);
        client_state->response_headers_len = strlen(proxy_result->headers);

        // for proxied body i might need to treat it as a memory buffer to send
        // or a temporary file but for temporary simplicity i'll store it as a 'file' in a conceptual way
        // bbut a more robust solution would be to manage this as a separate state within ClientState
        // for now, i'll create a dummy HttpResponse to hold the body data if `sendfile` isn't used
        // or i can send the body directly from main.c if it's in a buffer
        // for sendfile, i'd write `proxy_result->body` to a temp file and use its fd
        // so this is a simplification
        if (proxy_result->body) {
             // create a dummy response to hold body for send (i know it's not ideal)
            response = malloc(sizeof(HttpResponse));
            if (response) {
                memset(response, 0, sizeof(HttpResponse));
                response->file_fd = -1; // no actual file just body in memory
                // i'd need a mechanism to pass proxy_result->body to main.c for sending
                // this indicates a conceptual gap for direct proxy body sending
                // for now i'll assume that proxy_result->body is directly sent in main.c
                // and proxy_result is stored in client_state.
                // but the current main.c expects a file_fd
                // i think a better approach would be to have ClientState hold ProxyResult* directly
                // for simplicity of this example again i'll assume proxy_result->body is small and sent with headers or managed differently.
                // if the proxy result body is large i'd need a temporary file or a custom send loop

                // so (again) i'll assume for now proxy_result->body is not too large and is concatenated
                // with headers for simplicity in this example or i create a temp file
                // !! would like to note that this this is a major simplification !!
                // for a true non blocking proxy i'd have to read from the proxy socket and write to the client socket
                // wgilst managing both FDs with epoll
            } else {
                // log: malloc failure for dummy proxy response
                log_error("malloc for dummy httpresponse (proxy case) failed for client %s", client_ip_str);
            }
        }
        client_state->response = response; // store for freeing
        // clean up proxy_result now that its data is used
        free(proxy_result->headers);
        free(proxy_result->body);
        free(proxy_result);

        client_state->state = STATE_SENDING_HEADERS; // ready to send proxy headers
        // log: access log for proxied request
        if (request) { // check request for logging purposes
            int proxied_status_code = (response && response->status_line.status_code != 0) ? response->status_line.status_code : 200; // try to get actual status or default
            size_t proxied_body_len = (response && response->file_size != 0) ? response->file_size : 0; // try to get actual size or default
            log_access("%s \"%s %s %s\" %d %zu",
                       client_ip_str,
                       request->request_line.method,
                       request->request_line.path,
                       request->request_line.version,
                       proxied_status_code,
                       proxied_body_len);
        } else {
            log_access("%s \"proxied request parse failed\" 500 -", client_ip_str);
        }
        return; // done processing for this event
    }

    // --- method specific handling ---
    int num_handlers = sizeof(handlers) / sizeof(handlers[0]);
    int handler_found = 0;
    for (int i = 0; i < num_handlers; i++) {
        if (strcmp(request->request_line.method, handlers[i].method) == 0) {
            response = handlers[i].handler(request);
            handler_found = 1;
            break;
        }
    }
    if (!handler_found) {
        response = create_response(405, NULL); // method not allowed
    }

prepare_response_headers:
    client_state->response = response; // store generated response

    if (response) {
        client_state->response_headers_buffer = serialise_response(response);
        if (client_state->response_headers_buffer) {
            client_state->response_headers_len = strlen(client_state->response_headers_buffer);
            client_state->response_headers_sent = 0; // reset sent count
            client_state->state = STATE_SENDING_HEADERS;
            client_state->file_send_offset = 0; // reset file offset
        } else {
            // failed to serialise response so close connection
            // log: failed to serialize response
            log_error("failed to serialize response for fd %d (client %s)", client_state->fd, client_ip_str);
            fprintf(stderr, "Failed to serialize response for fd %d\n", client_state->fd);
            client_state->state = STATE_CLOSING; // signal main to close
        }
    } else {
        // no resposne generated
        // log: no response generated
        log_error("no response generated for fd %d (client %s)", client_state->fd, client_ip_str);
        fprintf(stderr, "No response generated for fd %d\n", client_state->fd);
        client_state->state = STATE_CLOSING; // signal main to close
    }

    // log: final access log for non-proxied requests
    if (request && response) {
        log_access("%s \"%s %s %s\" %d %zu",
                   client_ip_str,
                   request->request_line.method,
                   request->request_line.path,
                   request->request_line.version,
                   response->status_line.status_code,
                   response->file_size);
    } else if (request) {
        // a request was parsed, but response generation failed (e.g., malloc in create_response)
        log_access("%s \"%s %s %s\" %d -", // "-" for unknown body size
                   client_ip_str,
                   request->request_line.method,
                   request->request_line.path,
                   request->request_line.version,
                   500); // assume 500 internal server error
    } else {
        // request wasn't even parsed (e.g., 400 bad request)
        log_access("%s \"-\" %d -", client_ip_str, 400); // "-" for unknown request line and body size
    }
}
