#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "server.h"
#include "timer_wheel.h"

static timer_node_t *timer_wheel[WHEEL_SIZE];
static int current_tick = 0;

void timer_init() {
  for (int i = 0; i < WHEEL_SIZE; ++i) {
    timer_wheel[i] = NULL;
  }
}

void add_timer(client_t *client, int timeout_ms) {
  if (timeout_ms <= 0) {
    timeout_ms = 1;
  }
  int ticks_to_add = (timeout_ms / 1000) / TICK_INTERVAL_SECONDS;
  int slot = (current_tick + ticks_to_add) % WHEEL_SIZE;
  timer_node_t *node = malloc(sizeof(timer_node_t));
  if (!node) {
    perror("Failed to allocate timer node");
    return;
  }
  node->client = client;
  node->prev = NULL;
  node->next = timer_wheel[slot];
  if (timer_wheel[slot] != NULL) {
    timer_wheel[slot]->prev = node;
  }
  timer_wheel[slot] = node;
  client->timer_node = node;
}

void remove_timer(client_t *client) {
  timer_node_t *node = client->timer_node;
  if (!node)
    return;
  if (node->prev) {
    node->prev->next = node->next;
  } else {
    for (int i = 0; i < WHEEL_SIZE; ++i) {
      if (timer_wheel[i] == node) {
        timer_wheel[i] = node->next;
        break;
      }
    }
  }
  if (node->next) {
    node->next->prev = node->prev;
  }
  free(node);
  client->timer_node = NULL;
}

void tick_timer_wheel() {
  current_tick = (current_tick + 1) % WHEEL_SIZE;
  timer_node_t *current = timer_wheel[current_tick];
  timer_wheel[current_tick] = NULL;
  while (current != NULL) {
    timer_node_t *next = current->next;
    client_t *client = current->client;

    remove_timer(client);
    close_connection(client);
    current = next;
  }
}

