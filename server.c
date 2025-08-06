#include <errno.h> // For errno and EWOULDBLOCK/EAGAIN
#include <fcntl.h> // For fcntl (file control) to set non-blocking mode
#include <glib.h>
#include <netinet/in.h> // For sockaddr_in structure
#include <signal.h>     // --- NEW: For signal handling (SIGINT) ---
#include <stdio.h>      // For standard I/O functions like printf, perror
#include <stdlib.h>     // For general utilities like exit, malloc, free
#include <string.h>     // For string manipulation like memset, strlen, strstr
#include <sys/epoll.h>  // For epoll functions
#include <sys/select.h> // For FD_SETSIZE, a common max FD limit
#include <sys/socket.h> // For socket, bind, listen, accept functions
#include <time.h>       // For time() function to get current time
#include <unistd.h>     // For close function, read, write

#include "minheap_util.h"
#include "parser.h"
#include "server.h"

// Global counter for active client connections
static int active_clients_count = 0;

// --- NEW: Global flag to control the main server loop ---
// 'volatile' ensures the compiler doesn't optimize away checks on this
// variable, as it can be changed by an external signal handler.
volatile int running = 1;

GHashTable *client_states_map = NULL;

// Function to set a file descriptor (like a socket) to non-blocking mode.
// This means operations like 'accept' or 'read' on this descriptor won't
// pause the program if they can't complete immediately.
int set_nonblocking(int fd) {
  // Get the current flags associated with the file descriptor.
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL failed");
    return -1;
  }
  // Add the O_NONBLOCK flag to the existing flags.
  // O_NONBLOCK tells the OS that I/O operations should return immediately
  // if they would block, typically with an EAGAIN or EWOULDBLOCK error.
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL O_NONBLOCK failed");
    return -1;
  }
  return 0; // Return 0 on success, -1 on failure
}

// Function to set up the listening socket (create, bind, listen, set
// non-blocking) Returns the listening socket FD on success, -1 on failure.
int setup_listening_socket(int port) {
  int listen_sock;
  struct sockaddr_in server_addr;
  int opt = 1;

  // 1. Create a listening socket
  // AF_INET: Use IPv4 addresses
  // SOCK_STREAM: Use TCP (reliable, connection-oriented)
  // 0: Let the system choose the appropriate protocol (TCP for SOCK_STREAM)
  listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock == -1) {
    perror("socket creation failed");
    return -1;
  }

  // Optional: Set SO_REUSEADDR to allow immediate reuse of the port
  // This is useful during development to avoid "Address already in use" errors
  // if the server is restarted quickly.
  if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    perror("setsockopt SO_REUSEADDR failed");
    close(listen_sock);
    return -1;
  }

  // Prepare the server address structure
  memset(&server_addr, 0, sizeof(server_addr)); // Clear the structure to zeros
  server_addr.sin_family = AF_INET;             // IPv4 address family
  server_addr.sin_addr.s_addr =
      INADDR_ANY; // Listen on all available network interfaces
  server_addr.sin_port =
      htons(port); // Convert port number to network byte order

  // 2. Bind the listening socket to the specified IP address and port
  // This assigns the address to the socket.
  if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    perror("bind failed");
    close(listen_sock);
    return -1;
  }

  // 3. Start listening for incoming connections
  // 10: The maximum length of the queue of pending connections.
  if (listen(listen_sock, 10) == -1) {
    perror("listen failed");
    close(listen_sock);
    return -1;
  }

  printf("Server is listening for connections on port %d...\n", port);

  // 4. Set the listening socket to non-blocking mode
  // This is crucial for epoll. 'accept' on this socket won't block the server.
  if (set_nonblocking(listen_sock) == -1) {
    close(listen_sock);
    return -1;
  }

  return listen_sock;
}

// Function to handle a new incoming connection on the listening socket.
// It accepts all pending connections and adds them to the epoll instance.
void handle_new_connection(int listen_sock, int epoll_fd) {
  int conn_sock;
  struct sockaddr_in client_addr;
  socklen_t client_len;
  struct epoll_event event;
  client_state_t *client_state;

  while (1) {
    client_len = sizeof(client_addr);
    conn_sock =
        accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
    if (conn_sock == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("accept failed");
        exit(EXIT_FAILURE);
      }
    }

    // This check is now better handled by memory limits, as there is no fixed
    // array size if (active_clients_count >= MAX_ACTIVE_CLIENTS) { ... }

    printf("New connection accepted on socket %d. Active clients: %d\n",
           conn_sock, active_clients_count + 1);

    if (set_nonblocking(conn_sock) == -1) {
      close(conn_sock);
      continue;
    }

    client_state = (client_state_t *)malloc(sizeof(client_state_t));
    if (client_state == NULL) {
      perror("malloc client_state failed");
      close(conn_sock);
      continue;
    }
    memset(client_state, 0, sizeof(client_state_t));
    client_state->fd = conn_sock;
    client_state->state = READING_REQUEST;
    client_state->last_activity_time = time(NULL);
    client_state->timeout_heap_index =
        -1; // <-- NEW: Initialize the index to -1

    // Store client state in the Glib hash table
    g_hash_table_insert(client_states_map, GINT_TO_POINTER(conn_sock),
                        client_state);

    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = client_state; // Associate the event directly with our
                                   // client_state_t pointer
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
      perror("epoll_ctl: adding conn_sock failed");
      g_hash_table_remove(client_states_map,
                          GINT_TO_POINTER(conn_sock)); // Clean up the map
      free(client_state);
      close(conn_sock);
      continue;
    }

    active_clients_count++;
  }
}

// Function to handle reading data from a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_read_event(client_state_t *client_state, int epoll_fd) {
  int current_fd = client_state->fd;

  // Remove client from the timeout heap if it's currently on there
  remove_timeout_by_fd(current_fd);

  while (1) {
    ssize_t bytes_transferred;
    size_t bytes_to_read;
    char *read_destination;

    // Determine where to read based on the current state
    if (client_state->state == READING_REQUEST) {
      bytes_to_read =
          sizeof(client_state->in_buffer) - 1 - client_state->in_buffer_len;
      read_destination = client_state->in_buffer + client_state->in_buffer_len;
    } else if (client_state->state == READING_BODY) {
      bytes_to_read =
          client_state->content_length - client_state->body_received;
      read_destination =
          client_state->body_buffer + client_state->body_received;
    } else {
      break; // Not in a reading state
    }

    if (bytes_to_read == 0) {
      break;
    }

    bytes_transferred = read(current_fd, read_destination, bytes_to_read);

    if (bytes_transferred == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("read client socket failed");
        return 1;
      }
    } else if (bytes_transferred == 0) {
      printf("Client socket %d closed connection during read.\n", current_fd);
      return 1;
    }

    client_state->last_activity_time = time(NULL);

    if (client_state->state == READING_REQUEST) {
      client_state->in_buffer_len += bytes_transferred;
      client_state->in_buffer[client_state->in_buffer_len] = '\0';

      if (strstr(client_state->in_buffer, "\r\n\r\n")) {
        parse_http_request(client_state);
      }
    } else if (client_state->state == READING_BODY) {
      client_state->body_received += bytes_transferred;
      if (client_state->body_received >= client_state->content_length) {
        client_state->body_buffer[client_state->content_length] = '\0';
        printf("Full body received for client %d. Total size: %zu.\n",
               current_fd, client_state->content_length);
        printf("Final Request Body:\n%s\n", client_state->body_buffer);
        client_state->state = WRITING_RESPONSE;
      }
    }

    if (client_state->state == WRITING_RESPONSE) {
      create_http_response(client_state);
      struct epoll_event new_event;
      new_event.events = EPOLLOUT | EPOLLET;
      new_event.data.ptr = client_state;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, current_fd, &new_event) == -1) {
        perror("epoll_ctl: MOD to EPOLLOUT failed");
        return 1;
      }
      break;
    }
  }
  return 0;
}

// Function to handle writing data to a client socket
// Returns 1 if the connection should be closed, 0 otherwise.
int handle_write_event(client_state_t *client_state, int epoll_fd) {
  int current_fd = client_state->fd;
  ssize_t bytes_transferred;

  if (client_state->state != WRITING_RESPONSE) {
    printf("Client %d received EPOLLOUT but not in WRITING_RESPONSE state. "
           "Closing.\n",
           current_fd);
    return 1;
  }

  size_t remaining_to_send =
      client_state->out_buffer_len - client_state->out_buffer_sent;

  if (remaining_to_send > 0) {
    bytes_transferred = write(
        current_fd, client_state->out_buffer + client_state->out_buffer_sent,
        remaining_to_send);

    if (bytes_transferred == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        printf("Client socket %d send buffer full, retrying write.\n",
               current_fd);
        return 0;
      } else {
        perror("write client socket failed");
        return 1;
      }
    } else if (bytes_transferred == 0) {
      printf("Client socket %d closed connection during write.\n", current_fd);
      return 1;
    } else {
      client_state->out_buffer_sent += bytes_transferred;
      printf("Sent %zd bytes to client %d. Total sent: %zu/%zu.\n",
             bytes_transferred, current_fd, client_state->out_buffer_sent,
             client_state->out_buffer_len);

      client_state->last_activity_time = time(NULL);

      if (client_state->out_buffer_sent >= client_state->out_buffer_len) {
        printf("All response data sent to client %d.\n", current_fd);
        if (client_state->keep_alive) {
          printf("Keeping connection %d alive for next request.\n", current_fd);
          client_state->state = READING_REQUEST;
          client_state->in_buffer_len = 0;
          client_state->out_buffer_len = 0;
          client_state->out_buffer_sent = 0;

          time_t expires_at = time(NULL) + KEEPALIVE_IDLE_TIMEOUT_SECONDS;
          add_timeout(client_state->fd, expires_at);

          struct epoll_event new_event;
          new_event.events = EPOLLIN | EPOLLET;
          new_event.data.ptr = client_state;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, current_fd, &new_event) ==
              -1) {
            perror("epoll_ctl: MOD to EPOLLIN for keep-alive failed");
            return 1;
          }
          return 0;
        } else {
          printf("Closing connection %d (Keep-Alive not requested).\n",
                 current_fd);
          return 1;
        }
      }
      return 0;
    }
  } else {
    printf("EPOLLOUT on client %d but nothing to send. Closing.\n", current_fd);
    return 1;
  }
}

// Function to handle closing a client connection
void close_client_connection(int epoll_fd, client_state_t *client_state) {
  if (client_state == NULL) {
    return;
  }

  int current_fd = client_state->fd;

  // Remove the client from the timeout heap
  remove_timeout_by_fd(current_fd);

  if (client_state->body_buffer != NULL) {
    free(client_state->body_buffer);
    client_state->body_buffer = NULL;
  }

  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, current_fd, NULL) == -1 &&
      errno != ENOENT) {
    perror("epoll_ctl: DEL client socket failed");
  }

  close(current_fd);

  g_hash_table_remove(client_states_map, GINT_TO_POINTER(current_fd));

  free(client_state);
  printf("Client socket %d fully closed and removed.\n", current_fd);

  active_clients_count--;
  printf("Active clients: %d\n", active_clients_count);
}

// Function to handle events on an existing client socket.
// It orchestrates calls to read, write, or close functions based on event flags
// and client state.
void handle_client_event(int epoll_fd, struct epoll_event *event_ptr) {
  client_state_t *client_state = (client_state_t *)event_ptr->data.ptr;
  if (client_state == NULL) {
    // This should not happen, but it's a defensive check.
    fprintf(stderr, "handle_client_event: client_state is NULL.\n");
    return;
  }
  int current_fd = client_state->fd;
  uint32_t event_flags = event_ptr->events;
  int should_close = 0;

  if (event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    printf("Client socket %d disconnected or error occurred.\n", current_fd);
    should_close = 1;
  } else if (event_flags & EPOLLIN) {
    should_close = handle_read_event(client_state, epoll_fd);
  } else if (event_flags & EPOLLOUT) {
    should_close = handle_write_event(client_state, epoll_fd);
  }

  if (should_close) {
    close_client_connection(epoll_fd, client_state);
  }
}

// --- NEW: Signal handler function for SIGINT (Ctrl+C) ---
void handle_sigint(int sig) {
  printf("\nSIGINT received. Shutting down server gracefully...\n");
  running = 0; // Set the global flag to stop the main loop
}

void cleanup_client_state(client_state_t *client_state, int epoll_fd) {
  if (client_state->body_buffer != NULL) {
    free(client_state->body_buffer);
    client_state->body_buffer =
        NULL; // Best practice to avoid dangling pointers
  }

  // Remove the file descriptor from the epoll instance
  if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_state->fd, NULL) == -1) {
    perror("epoll_ctl: DEL failed in cleanup");
  }

  close(client_state->fd);
  free(client_state);
}

void print_heap_state() {
  printf("Timeout Heap State (Size: %zu):\n", heap_size);
  if (heap_size == 0) {
    printf("  [Empty]\n");
    return;
  }
  for (size_t i = 0; i < heap_size; ++i) {
    printf("  [%zu]: FD %d, Expires: %ld\n", i, timeout_heap[i].fd,
           timeout_heap[i].expires);
  }
}

int main() {
  int listen_sock; // The socket that listens for new incoming connections
  int epoll_fd;    // The file descriptor for our epoll instance
  struct epoll_event
      event; // A single epoll_event structure to configure events
  struct epoll_event
      events[MAX_EVENTS]; // Array to store events returned by epoll_wait
  int num_events;         // Number of events returned by epoll_wait
  int i;                  // Loop counter for processing events array

  // Initialize the Glib hash table
  client_states_map = g_hash_table_new(g_direct_hash, g_direct_equal);

  printf("Starting simple epoll server on port %d.\n", PORT);

  if (signal(SIGINT, handle_sigint) == SIG_ERR) {
    perror("signal registration failed");
    exit(EXIT_FAILURE);
  }

  // This loop for `FD_SETSIZE` is no longer needed
  // for (i = 0; i < FD_SETSIZE; i++) {
  //     client_states_map[i] = NULL;
  // }

  listen_sock = setup_listening_socket(PORT);
  if (listen_sock == -1) {
    exit(EXIT_FAILURE);
  }

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1 failed");
    close(listen_sock);
    exit(EXIT_FAILURE);
  }

  event.events = EPOLLIN | EPOLLET;
  event.data.fd = listen_sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) == -1) {
    perror("epoll_ctl: adding listen_sock failed");
    close(listen_sock);
    close(epoll_fd);
    exit(EXIT_FAILURE);
  }

  while (running) {
    long epoll_timeout_ms = get_next_timeout_ms();

    printf("--- Main Loop Pass ---\n");
    print_heap_state();
    printf("Next epoll_wait timeout: %ldms\n", epoll_timeout_ms);
    // This is a crucial check to prevent waiting indefinitely if the heap is
    // empty. It's also important to pass -1 to epoll_wait when the timeout is
    // not a positive number or 0. Let's make sure the get_next_timeout_ms
    // function returns a valid milliseconds value or -1.

    num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, epoll_timeout_ms);

    if (num_events == -1) {
      if (errno == EINTR) {
        if (!running) {
          break;
        }
        continue;
      }
      perror("epoll_wait failed");
      running = 0;
      continue;
    }

    for (i = 0; i < num_events; i++) {
      if (events[i].data.fd == listen_sock) {
        handle_new_connection(listen_sock, epoll_fd);
      } else {
        handle_client_event(epoll_fd, &events[i]);
      }
    }

    // Process any expired timeouts after epoll_wait returns
    time_t current_time;
    while (heap_size > 0) {
      current_time = time(NULL);

      // Check if the next timeout has expired
      if (timeout_heap[0].expires > current_time) {
        printf("DEBUG: Next timeout for FD %d is in the future. Remaining: %ld "
               "seconds.\n",
               timeout_heap[0].fd, timeout_heap[0].expires - current_time);
        break; // No more expired timeouts
      }

      int expired_fd = timeout_heap[0].fd;
      printf("DEBUG: Found expired timeout for FD %d. Current time: %ld, "
             "Expires: %ld.\n",
             expired_fd, current_time, timeout_heap[0].expires);

      client_state_t *client_state =
          g_hash_table_lookup(client_states_map, GINT_TO_POINTER(expired_fd));

      if (client_state != NULL) {
        printf("DEBUG: Found client state for FD %d. Closing connection.\n",
               expired_fd);
        close_client_connection(epoll_fd, client_state);
        // NOTE: The close_client_connection function should call
        // remove_timeout_by_fd(expired_fd) to remove this client from the heap.
      } else {
        printf("WARNING: Expired client FD %d not found in hash table. This is "
               "a logic error.\n",
               expired_fd);
        // If the client state is NULL, it means the client was removed from the
        // hash table without being removed from the heap. We must still clean
        // up the heap. We will call remove_min_timeout() below regardless.
      }

      // Always remove the expired element from the heap to prevent an infinite
      // loop
      remove_min_timeout();
      printf(
          "DEBUG: Removed expired FD %d from min-heap. New heap size: %zu.\n",
          expired_fd, heap_size);
    }
  }

  printf("Server shutting down. Cleaning up resources...\n");
  close(listen_sock);

  // --- NEW: A better way to close all connections is to iterate over the hash
  // table ---
  g_hash_table_destroy(client_states_map);

  close(epoll_fd);
  printf("Server shutdown complete.\n");

  return 0;
}
