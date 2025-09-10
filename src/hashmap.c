#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"

// Define the structure for a key-value pair node.
// These nodes will be used to create linked lists for separate chaining.
typedef struct Node {
  char *key;
  void *value; // Use a void pointer to store any type of value.
  struct Node *next;
} Node;

// Define the hash map structure.
typedef struct HashMap {
  Node **buckets;
} HashMap;

/**
 * @brief Creates and initializes a new hash map.
 * @return A pointer to the newly created HashMap, or NULL if memory allocation
 * fails.
 */
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

/**
 * @brief A simple hash function to calculate the index for a given key.
 * This function uses the ASCII values of the characters in the key.
 * @param key The string key to be hashed.
 * @return The hash index, which is an integer between 0 and TABLE_SIZE - 1.
 */
unsigned int hash_function(const char *key) {
  unsigned int hash_value = 0;
  for (int i = 0; key[i] != '\0'; i++) {
    hash_value = (hash_value * 31) +
                 key[i]; // A simple, efficient polynomial rolling hash.
  }
  return hash_value % TABLE_SIZE;
}

/**
 * @brief Inserts a key-value pair into the hash map.
 * If the key already exists, the value will be updated.
 * @param map A pointer to the HashMap.
 * @param key The string key to insert. A copy of this key is made.
 * @param value A pointer to the value to be stored.
 * @return 0 on success, -1 on failure (e.g., memory allocation error).
 */
int insert_hashmap(HashMap *map, const char *key, void *value) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];

  // Check for existing key and update value
  while (current != NULL) {
    if (strcmp(current->key, key) == 0) {
      // Free the old value before updating with the new one
      free(current->value);                         // ⚠️  Add this line
      current->value = strdup((const char *)value); // ⚠️  Add this line
      // Check for strdup failure
      if (!current->value) {
        // Handle error
      }
      return 0;
    }
    current = current->next;
  }

  // Key not found, create a new node
  Node *newNode = (Node *)malloc(sizeof(Node));
  if (!newNode) {
    return -1;
  }

  newNode->key = strdup(key);
  newNode->value = strdup((const char *)value); // ⚠️  Add this line
  newNode->next = map->buckets[index];
  map->buckets[index] = newNode;

  if (!newNode->key || !newNode->value) {
    // Handle allocation failure by freeing what was allocated
    free(newNode->key);
    free(newNode->value);
    free(newNode);
    return -1;
  }

  return 0;
}

/**
 * @brief Retrieves the value associated with a given key from the hash map.
 * @param map A pointer to the HashMap.
 * @param key The string key to search for.
 * @return A pointer to the stored value, or NULL if the key is not found.
 */
void *get_hashmap(const HashMap *map, const char *key) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];

  // Traverse the linked list at the calculated index.
  while (current != NULL) {
    // If the keys match, return the value.
    if (strcmp(current->key, key) == 0) {
      return current->value;
    }
    current = current->next;
  }
  // Key not found in the hash map.
  return NULL;
}

/**
 * @brief Deletes a key-value pair from the hash map.
 * @param map A pointer to the HashMap.
 * @param key The string key to delete.
 * @return 0 on success, -1 if the key was not found.
 */
int delete_hashmap(HashMap *map, const char *key) {
  unsigned int index = hash_function(key);
  Node *current = map->buckets[index];
  Node *prev = NULL;

  // Traverse the linked list to find the key.
  while (current != NULL && strcmp(current->key, key) != 0) {
    prev = current;
    current = current->next;
  }

  // If the key was not found in the list, return failure.
  if (current == NULL) {
    return -1;
  }

  // Unlink the node from the list.
  if (prev == NULL) {
    // Deleting the first node in the list.
    map->buckets[index] = current->next;
  } else {
    // Deleting a node in the middle or end of the list.
    prev->next = current->next;
  }

  // Free the memory for the deleted node.
  free(current->key);
  free(current);
  return 0; // Success.
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
    // After freeing all nodes in the list, set the bucket to NULL.
    map->buckets[i] = NULL;
  }
}

/**
 * @brief Frees all memory allocated for the hash map.
 * This is crucial to prevent memory leaks.
 * @param map A pointer to the HashMap.
 */
void free_hashmap(HashMap *map) {
  if (!map)
    return;

  for (int i = 0; i < TABLE_SIZE; i++) {
    Node *current = map->buckets[i];
    while (current != NULL) {
      Node *temp = current;
      current = current->next;
      free(temp->key);
      free(temp->value); // ⚠️  Add this line
      free(temp);
    }
  }
  free(map->buckets);
  free(map);
}

/*
// Main function to demonstrate the hash map functionality.
int main() {
  HashMap *my_map = create_hashmap();
  if (!my_map) {
    return 1;
  }

  // Example: Storing integer values.
  int a = 10, b = 20, c = 30;
  insert_hashmap(my_map, "key_a", &a);
  insert_hashmap(my_map, "key_b", &b);
  insert_hashmap(my_map, "key_c", &c);
  insert_hashmap(my_map, "key_b", &a); // Overwrite key_b value.

  // Retrieve values and print them.
  int *val_a = (int *)get_hashmap(my_map, "key_a");
  int *val_b = (int *)get_hashmap(my_map, "key_b");
  int *val_c = (int *)get_hashmap(my_map, "key_c");
  int *val_d = (int *)get_hashmap(my_map, "nonexistent_key");

  printf("Value for key 'key_a': %d\n", val_a ? *val_a : -1);
  printf("Value for key 'key_b': %d\n", val_b ? *val_b : -1);
  printf("Value for key 'key_c': %d\n", val_c ? *val_c : -1);
  printf("Value for key 'nonexistent_key': %s\n",
         val_d ? "Found" : "Not Found");

  // Delete a key-value pair and verify.
  delete_hashmap(my_map, "key_b");
  val_b = (int *)get_hashmap(my_map, "key_b");
  printf("\nAfter deleting 'key_b', value is: %s\n",
         val_b ? "Found" : "Not Found");

  // Free all allocated memory.
  free_hashmap(my_map);
  return 0;
}
*/
