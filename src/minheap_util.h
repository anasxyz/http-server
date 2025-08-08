#ifndef MINHEAP_UTIL_H
#define MINHEAP_UTIL_H

#include "glib.h"
#include <sys/types.h>
#include <time.h>

typedef struct {
    time_t expires;
    int fd;
} timeout_event_t;

extern timeout_event_t *timeout_heap;
extern size_t heap_size;
extern size_t heap_capacity;

void init_min_heap();
void swap(timeout_event_t *a, timeout_event_t *b, GHashTable *client_states_map);
void heapify_up(size_t index, GHashTable *client_states_map);
void heapify_down(size_t index, GHashTable *client_states_map);
void add_timeout(int fd, time_t expires, GHashTable *client_states_map);
void remove_min_timeout(GHashTable *client_states_map);
long get_next_timeout_ms();
void remove_timeout_by_fd(int fd, GHashTable *client_states_map);
void update_timeout(int fd, time_t expires, GHashTable *client_states_map);

#endif // MINHEAP_UTIL_H
