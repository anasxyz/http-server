#ifndef _TIMER_WHEEL_H_
#define _TIMER_WHEEL_H_

#include "server.h"

typedef struct timer_node timer_node_t;
#define WHEEL_SIZE 60
#define TICK_INTERVAL_SECONDS 1

typedef struct timer_node {
  client_t *client;
  timer_node_t *prev;
  timer_node_t *next;
} timer_node_t;

void timer_init();
void add_timer(client_t *client, int timeout_ms);
void remove_timer(client_t *client);
void tick_timer_wheel();

#endif // _TIMER_WHEEL_H_
