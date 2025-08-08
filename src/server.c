#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "connection_handler.h"
#include "minheap_util.h"
#include "server.h"
#include "utils.h"

volatile int running = 1;

// Function to set up the listening socket
int setup_listening_socket(int port) {
  int listen_sock;
  struct sockaddr_in server_addr;
  int opt = 1;

  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == -1) {
    fprintf(stderr, "ERROR: Failed to create a listening socket. The server "
                    "cannot start.\n");
#ifdef VERBOSE_MODE
    perror("REASON: socket creation failed");
#endif
    return -1;
  }

  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    fprintf(stderr, "ERROR: Failed to configure socket options. The server "
                    "cannot start.\n");
#ifdef VERBOSE_MODE
    perror("REASON: setsockopt SO_REUSEADDR failed");
#endif
    close(listen_sock);
    return -1;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    fprintf(stderr,
            "ERROR: Failed to bind the socket to port %d. The port may be in "
            "use.\n",
            port);
#ifdef VERBOSE_MODE
    perror("REASON: bind failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (listen(listen_sock, 10) == -1) {
    fprintf(stderr,
            "ERROR: Failed to prepare the socket for incoming connections.\n");
#ifdef VERBOSE_MODE
    perror("REASON: listen failed");
#endif
    close(listen_sock);
    return -1;
  }

  if (set_nonblocking(listen_sock) == -1) {
    fprintf(stderr, "ERROR: Failed to configure the listening socket for "
                    "non-blocking operations.\n");
    // The reason is already printed in set_nonblocking
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}


// Add this function to your server.c file
void cleanup_client_state_on_destroy(gpointer data) {
  client_state_t *client_state = (client_state_t *)data;

  if (client_state->body_buffer != NULL) {
    free(client_state->body_buffer);
    client_state->body_buffer = NULL;
  }

  free(client_state);
}


// Signal handler function for SIGINT (Ctrl+C)
void handle_sigint() {
  // printf("\nINFO: SIGINT received. Shutting down server gracefully...\n");
  running = 0;
}

// All your previous `main` function logic, including the `epoll` setup
// and the `while(running)` loop, now lives in this function.
void run_worker_loop(int listen_sock) {
    // Each worker has its own private set of state variables.
    GHashTable *client_states_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, cleanup_client_state_on_destroy);
    init_min_heap(); // Each worker also gets its own timeout heap.

    int epoll_fd;
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];
    int num_events;
    int i;
    
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        fprintf(stderr, "ERROR: Worker failed to create epoll instance.\n");
        exit(EXIT_FAILURE);
    }
    
    // A worker adds the shared listening socket to its own epoll instance.
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = listen_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
        fprintf(stderr, "ERROR: Worker failed to register listening socket.\n");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // The worker's main event loop.
    while (running) {
        long epoll_timeout_ms = get_next_timeout_ms();
        num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_timeout_ms);

        if (num_events == -1) {
            if (errno == EINTR) {
                if (!running) break;
                continue;
            }
            fprintf(stderr, "ERROR: Worker's epoll_wait failed.\n");
            break;
        }
        
        for (i = 0; i < num_events; i++) {
            if (events[i].data.fd == listen_sock) {
                handle_new_connection(listen_sock, epoll_fd, client_states_map);
            } else {
                handle_client_event(epoll_fd, &events[i], client_states_map);
            }
        }
        
        while (heap_size > 0) {
            time_t current_time = time(NULL);
            if (timeout_heap[0].expires > current_time) {
                break;
            }
            
            int expired_fd = timeout_heap[0].fd;
            client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(expired_fd));
            if (client_state != NULL) {
                printf("INFO: Client %d timed out. Closing connection.\n", expired_fd);
                close_client_connection(epoll_fd, client_state, client_states_map);
            } else {
                remove_min_timeout(client_states_map);
            }
        }
    }
    
    // Worker cleans up its own resources before exiting.
    printf("INFO: Worker %d shutting down.\n", getpid());
    g_hash_table_destroy(client_states_map);
    close(epoll_fd);
    free(timeout_heap);
}


int main() {
    int listen_sock;
    pid_t pid;
    int i;
    int status;

    // 1. The Master Process creates the single listening socket.
    printf("INFO: Master process starting, listening on port %d.\n", PORT);
    listen_sock = setup_listening_socket(PORT);
    if (listen_sock == -1) {
        fprintf(stderr, "ERROR: Master failed to setup listening socket.\n");
        exit(EXIT_FAILURE);
    }

    // Set up a signal handler for graceful shutdown in the master process.
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        fprintf(stderr, "ERROR: Master failed to set up an exit handler.\n");
        exit(EXIT_FAILURE);
    }
    
    // 2. The Master Process forks off multiple worker processes.
    for (i = 0; i < NUM_WORKERS; i++) {
        pid = fork();
        if (pid == -1) {
            perror("ERROR: Master failed to fork worker process");
            // In a real application, you might handle this more gracefully.
            continue; 
        } else if (pid == 0) { // This is the child process (the worker).
            printf("INFO: Worker process %d (PID: %d) started.\n", i, getpid());
            run_worker_loop(listen_sock);
            exit(EXIT_SUCCESS); // Worker exits after its loop terminates.
        }
    }

    // 3. The Master Process waits for all worker processes to terminate.
    // It will block here until all child processes have exited.
    while (wait(&status) > 0) {
        // You can check the status of the child process here if needed.
    }
    
    // 4. Master cleans up the listening socket and its own resources.
    printf("INFO: All worker processes have terminated. Master shutting down.\n");
    close(listen_sock);

    return 0;
}

