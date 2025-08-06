#include "minheap_util.h"
#include "glib.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>

timeout_event_t *timeout_heap = NULL;
size_t heap_size = 0;
size_t heap_capacity = 0;

void swap(timeout_event_t *a, timeout_event_t *b) {
    // Save the original positions
    size_t a_index = a - timeout_heap;
    size_t b_index = b - timeout_heap;

    // Perform the swap
    timeout_event_t temp = *a;
    *a = *b;
    *b = temp;

    // --- CRITICAL NULL CHECKS ---
    // Look up client states by their file descriptor (fd)
    client_state_t *client_a = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(a->fd));
    client_state_t *client_b = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(b->fd));

    // Update their heap index only if the lookup was successful
    if (client_a != NULL) {
        client_a->timeout_heap_index = a_index;
    } else {
        printf("ERROR: client_a is NULL for FD %d during swap!\n", a->fd);
    }

    if (client_b != NULL) {
        client_b->timeout_heap_index = b_index;
    } else {
        printf("ERROR: client_b is NULL for FD %d during swap!\n", b->fd);
    }
}

void heapify_up(size_t index) {
    if (index == 0) return;
    size_t parent_index = (index - 1) / 2;
    if (timeout_heap[index].expires < timeout_heap[parent_index].expires) {
        swap(&timeout_heap[index], &timeout_heap[parent_index]);
        heapify_up(parent_index);
    }
}

void heapify_down(size_t index) {
    size_t smallest = index;
    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;

    if (left < heap_size && timeout_heap[left].expires < timeout_heap[smallest].expires) {
        smallest = left;
    }
    if (right < heap_size && timeout_heap[right].expires < timeout_heap[smallest].expires) {
        smallest = right;
    }

    if (smallest != index) {
        swap(&timeout_heap[index], &timeout_heap[smallest]);
        heapify_down(smallest);
    }
}

void add_timeout(int fd, time_t expires) {
    printf("DEBUG: Attempting to add FD %d to heap. Current size: %zu.\n", fd, heap_size);
    if (heap_size >= heap_capacity) {
        heap_capacity = (heap_capacity == 0) ? 10 : heap_capacity * 2;
        printf("DEBUG: Reallocating heap to new capacity: %zu\n", heap_capacity);
        timeout_heap = realloc(timeout_heap, heap_capacity * sizeof(timeout_event_t));
        if (timeout_heap == NULL) {
            perror("realloc failed for timeout heap");
            return;
        }
    }
    
    // Check if the client_state exists before adding
    client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(fd));
    if (client_state == NULL) {
        printf("ERROR: client_state for FD %d not found in hash table during add_timeout.\n", fd);
        return; // Prevents a potential crash later
    }
    
    timeout_heap[heap_size].fd = fd;
    timeout_heap[heap_size].expires = expires;
    
    // Update the index in the client_state struct
    client_state->timeout_heap_index = heap_size;
    
    heap_size++;
    heapify_up(heap_size - 1);
    printf("DEBUG: Successfully added FD %d. New heap size: %zu.\n", fd, heap_size);
}

void remove_min_timeout() {
    if (heap_size == 0) return;
    timeout_heap[0] = timeout_heap[heap_size - 1];
    heap_size--;
    if (heap_size > 0) {
        heapify_down(0);
    }
}

// Updated function to get the next timeout
long get_next_timeout_ms() {
    if (heap_size == 0) {
        return -1; // Wait indefinitely
    }
    time_t current_time = time(NULL);
    long remaining_seconds = timeout_heap[0].expires - current_time;
    if (remaining_seconds <= 0) {
        return 0; // Timeout already expired, return immediately
    }
    return remaining_seconds * 1000;
}

ssize_t find_fd_index(int fd) {
    for (size_t i = 0; i < heap_size; ++i) {
        if (timeout_heap[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

void remove_timeout_by_fd(int fd) {
    client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(fd));
    
    // Check if the client exists and is actually in the heap
    if (client_state == NULL || client_state->timeout_heap_index == -1) {
        return;
    }
    
    ssize_t index = client_state->timeout_heap_index;
    
    // Move the last element into the position of the one being removed
    timeout_heap[index] = timeout_heap[heap_size - 1];
    heap_size--;
    
    // Update the index of the element that was moved
    if (heap_size > 0 && index < heap_size) {
        client_state_t *moved_client = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(timeout_heap[index].fd));
        if (moved_client != NULL) {
            moved_client->timeout_heap_index = index;
        }
        
        // Re-heapify from the new position to fix the heap property
        heapify_down(index);
        heapify_up(index);
    }
    
    // Clear the heap index for the removed client
    client_state->timeout_heap_index = -1;
}
