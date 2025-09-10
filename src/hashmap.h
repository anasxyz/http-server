#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define the maximum size of the hash table.
// Using a prime number helps in distributing keys more evenly,
// reducing collisions and improving performance.
#define TABLE_SIZE 10007

// Use an opaque pointer for the HashMap type. This hides the internal
// structure from the user, providing a higher level of abstraction.
typedef struct HashMap HashMap;

/**
 * @brief Creates and initializes a new hash map.
 * @return A pointer to the newly created HashMap, or NULL if memory allocation
 * fails.
 */
HashMap *create_hashmap();

/**
 * @brief Inserts a key-value pair into the hash map.
 * If the key already exists, the value will be updated.
 * @param map A pointer to the HashMap.
 * @param key The string key to insert.
 * @param value A pointer to the value to be stored.
 * @return 0 on success, -1 on failure (e.g., memory allocation error).
 */
int insert_hashmap(HashMap *map, const char *key, void *value);

/**
 * @brief Retrieves the value associated with a given key from the hash map.
 * @param map A pointer to the HashMap.
 * @param key The string key to search for.
 * @return A pointer to the stored value, or NULL if the key is not found.
 */
void *get_hashmap(const HashMap *map, const char *key);

/**
 * @brief Deletes a key-value pair from the hash map.
 * @param map A pointer to the HashMap.
 * @param key The string key to delete.
 * @return 0 on success, -1 if the key was not found.
 */
int delete_hashmap(HashMap *map, const char *key);

void clear_hashmap(HashMap *map);

/**
 * @brief Frees all memory allocated for the hash map.
 * This is crucial to prevent memory leaks.
 * @param map A pointer to the HashMap.
 */
void free_hashmap(HashMap *map);

#endif // HASHMAP_H
