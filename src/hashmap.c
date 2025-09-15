#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"

typedef struct Node {
  char *key;
  void *value;
  struct Node *next;
} Node;

typedef struct HashMap {
  Node **buckets;
} HashMap;

HashMap *create_hashmap() {
  HashMap *map = (HashMap *)malloc(sizeof(HashMap));
  if (!map) {
    perror("Failed to allocate memory for HashMap");
    return NULL;
  }
  map->buckets = (Node **)calloc(TABLE_SIZE, sizeof(Node *));
  if (!map->buckets) {
    perror("Failed to allocate memory for buckets");
    free(map);
    return NULL;
  }
  return map;
}

unsigned int hash_function(const char *key) {
  unsigned int hash_value = 0;
  for (int i = 0; key[i] != '\0'; i++) {
    hash_value = (hash_value * 31) +
                 key[i];
  }
  return hash_value % TABLE_SIZE;
}

int insert_hashmap(HashMap *map, const char *key, void *value) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];

  // check for existing key and update value
  while (current != NULL) {
    if (strcmp(current->key, key) == 0) {
      free(current->value);
      current->value = strdup((const char *)value);
      if (!current->value) {
				// handle error
      }
      return 0;
    }
    current = current->next;
  }

  // key not found, create a new node
  Node *newNode = (Node *)malloc(sizeof(Node));
  if (!newNode) {
    return -1;
  }

  newNode->key = strdup(key);
  newNode->value = strdup((const char *)value);
  newNode->next = map->buckets[index];
  map->buckets[index] = newNode;

  if (!newNode->key || !newNode->value) {
    free(newNode->key);
    free(newNode->value);
    free(newNode);
    return -1;
  }

  return 0;
}

void *get_hashmap(const HashMap *map, const char *key) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];

  while (current != NULL) {
    // if the keys match, return the value
    if (strcmp(current->key, key) == 0) {
      return current->value;
    }
    current = current->next;
  }
  return NULL;
}

int delete_hashmap(HashMap *map, const char *key) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];
  Node *prev = NULL;

  // traverse the linked list to find the key
  while (current != NULL && strcmp(current->key, key) != 0) {
    prev = current;
    current = current->next;
  }

  // if the key was not found in the list, return failure
  if (current == NULL) {
    return -1;
  }

  // unlink the node from the list
  if (prev == NULL) {
    // deleting the first node in the list
    map->buckets[index] = current->next;
  } else {
    // deleting a node in the middle or end of the list
    prev->next = current->next;
  }

  free(current->key);
  free(current);
  return 0;
}

void clear_hashmap(HashMap *map) {
  if (!map) {
    return;
  }

  for (int i = 0; i < TABLE_SIZE; i++) {
    Node *current = map->buckets[i];
    while (current != NULL) {
      Node *temp = current;
      current = current->next;
      free(temp->key);
      free(temp->value);
      free(temp);
    }
    map->buckets[i] = NULL;
  }
}

void free_hashmap(HashMap *map) {
  if (!map)
    return;

  for (int i = 0; i < TABLE_SIZE; i++) {
    Node *current = map->buckets[i];
    while (current != NULL) {
      Node *temp = current;
      current = current->next;
      free(temp->key);
      free(temp->value);
      free(temp);
    }
  }
  free(map->buckets);
  free(map);
}
