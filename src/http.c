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

#include "../include/config.h" // Now includes the Config struct and accessors
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
    if (!response)
        return NULL;

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
            free_response(response);
            return create_response(500, NULL); // internal Server Error
        }
        // open file
        response->file_fd = open(path, O_RDONLY | O_NONBLOCK); // open non blocking
        if (response->file_fd == -1) {
            perror("Error opening requested file");
            // could be a 404 if logic failed to catch it or a 403 for pwermissions
            free_response(response);
            return create_response(404, NULL); // Or 403 depending on errno
        }

        // get file size
        struct stat st;
        if (fstat(response->file_fd, &st) == -1) {
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
    char *last_modified = http_date_now(); // change this later to the actual file's last modified time

    set_header(response, "Server", "http-server");
    set_header(response, "Date", date);
    set_header(response, "Connection", "close"); // start with close persistent can be added later
    set_header(response, "Content-Type", content_type);
    set_header(response, "Content-Length", content_length_str);
    set_header(response, "Last-Modified", last_modified);

    free(date);
    free(last_modified);

    return response;
}

// serialise response to string.
// this function will now return a dynamically allocated string containing the headers
// i changed it so that it will NOT include the body, the file will be sent separately with sendfile()
char *serialise_response(HttpResponse *response) {
    // here i just estimate the size so just max headers + status line + CRLF
    int size = 1024 + (response->header_count * 128);
    char *buffer = malloc(size);
    if (!buffer)
        return NULL;

    int offset = snprintf(
        buffer, size, "%s %d %s\r\n", response->status_line.http_version,
        response->status_line.status_code, response->status_line.status_reason);

    for (size_t i = 0; i < response->header_count; i++) {
        offset += snprintf(buffer + offset, size - offset, "%s: %s\r\n",
                           response->headers[i].key, response->headers[i].value);
    }

    offset += snprintf(buffer + offset, size - offset, "\r\n");
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
        return create_response(500, NULL);
    }

    // --- try files from config ---
    // try_paths now internally uses get_current_config()
    resolved_path = try_paths(path);

    if (!resolved_path) {
        // path doesn't exist - page not found
        response = create_response(404, NULL);
        goto end; // goto for cleanup before returning
    }

    // if path exists
    response = create_response(200, resolved_path);
    if (!response) { // If create_response failed for 200 OK (e.g., file open error)
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

    // Get the current active configuration
    Config *current_cfg = get_current_config();
    if (!current_cfg) {
        fprintf(stderr, "Error: No active configuration found for request handling!\n");
        response = create_response(500, NULL); // Internal Server Error
        goto prepare_response_headers;
    }

    // parse the request from the accumulated read_buffer
    request = parse_request(client_state->read_buffer);
    client_state->request = request; // store for later freeing

    if (!request) {
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
            }
        }
        client_state->response = response; // store for freeing
        // clean up proxy_result now that its data is used
        free(proxy_result->headers);
        free(proxy_result->body);
        free(proxy_result);

        client_state->state = STATE_SENDING_HEADERS; // ready to send proxy headers
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
            fprintf(stderr, "Failed to serialize response for fd %d\n", client_state->fd);
            client_state->state = STATE_CLOSING; // signal main to close
        }
    } else {
        // no resposne generated
        fprintf(stderr, "No response generated for fd %d\n", client_state->fd);
        client_state->state = STATE_CLOSING; // signal main to close
    }
}
