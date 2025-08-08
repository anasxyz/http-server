#ifndef MINHEAP_UTIL_H
#define MINHEAP_UTIL_H

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
void swap(timeout_event_t *a, timeout_event_t *b);
void heapify_up(size_t index);
void heapify_down(size_t index);
void add_timeout(int fd, time_t expires);
void remove_min_timeout();
long get_next_timeout_ms();
ssize_t find_fd_index(int fd);
void remove_timeout_by_fd(int fd);
void update_timeout(int fd, time_t expires);

#endif // MINHEAP_UTIL_H
