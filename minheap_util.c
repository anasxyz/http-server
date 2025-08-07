#include "minheap_util.h"
#include "glib.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

timeout_event_t *timeout_heap = NULL;
size_t heap_size = 0;
size_t heap_capacity = 0;

// Function to initialize the min-heap
void init_min_heap() {
    timeout_heap = NULL;
    heap_size = 0;
    heap_capacity = 0;
}

void swap(timeout_event_t *a, timeout_event_t *b) {
    size_t a_index = a - timeout_heap;
    size_t b_index = b - timeout_heap;

    client_state_t *client_a = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(a->fd));
    client_state_t *client_b = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(b->fd));

    timeout_event_t temp = *a;
    *a = *b;
    *b = temp;

    if (client_a != NULL) {
        client_a->timeout_heap_index = b_index;
    }
    if (client_b != NULL) {
        client_b->timeout_heap_index = a_index;
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
    if (heap_size >= heap_capacity) {
        heap_capacity = (heap_capacity == 0) ? 10 : heap_capacity * 2;
        timeout_event_t *new_heap = realloc(timeout_heap, heap_capacity * sizeof(timeout_event_t));
        if (new_heap == NULL) {
            perror("realloc failed for timeout heap");
            return;
        }
        timeout_heap = new_heap;
    }
    
    client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(fd));
    if (client_state == NULL) {
        return;
    }
    
    timeout_heap[heap_size].fd = fd;
    timeout_heap[heap_size].expires = expires;
    client_state->timeout_heap_index = heap_size;
    
    heap_size++;
    heapify_up(heap_size - 1);
}

void remove_min_timeout() {
    if (heap_size == 0) return;

    int removed_fd = timeout_heap[0].fd;
    client_state_t *removed_client = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(removed_fd));
    if (removed_client != NULL) {
        removed_client->timeout_heap_index = -1;
    }

    timeout_heap[0] = timeout_heap[heap_size - 1];
    heap_size--;

    if (heap_size > 0) {
        client_state_t *moved_client = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(timeout_heap[0].fd));
        if (moved_client != NULL) {
            moved_client->timeout_heap_index = 0;
        }
        heapify_down(0);
    }
}

long get_next_timeout_ms() {
    if (heap_size == 0) {
        return -1;
    }
    time_t current_time = time(NULL);
    long remaining_seconds = timeout_heap[0].expires - current_time;
    if (remaining_seconds <= 0) {
        return 0;
    }
    return remaining_seconds * 1000;
}

void remove_timeout_by_fd(int fd) {
    client_state_t *client_state = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(fd));
    
    if (client_state == NULL || client_state->timeout_heap_index == -1) {
        return;
    }
    
    size_t index = client_state->timeout_heap_index;
    
    client_state->timeout_heap_index = -1;
    
    timeout_heap[index] = timeout_heap[heap_size - 1];
    heap_size--;

    if (heap_size > 0 && index < heap_size) {
        client_state_t *moved_client = g_hash_table_lookup(client_states_map, GINT_TO_POINTER(timeout_heap[index].fd));
        if (moved_client != NULL) {
            moved_client->timeout_heap_index = index;
        }

        if (index > 0 && timeout_heap[index].expires < timeout_heap[(index - 1) / 2].expires) {
            heapify_up(index);
        } else {
            heapify_down(index);
        }
    }
}
