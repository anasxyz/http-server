#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// define the maximum size of the hash table.
// using a prime number helps in distributing keys more evenly,
// reducing collisions and improving performance.
#define TABLE_SIZE 10007

typedef struct HashMap HashMap;

/**
 * @brief creates and initializes a new hash map.
 * @return a pointer to the newly created hashmap, or null if memory allocation
 * fails.
 */
HashMap *create_hashmap();

/**
 * @brief inserts a key-value pair into the hash map.
 * if the key already exists, the value will be updated.
 * @param map a pointer to the hashmap.
 * @param key the string key to insert.
 * @param value a pointer to the value to be stored.
 * @return 0 on success, -1 on failure (e.g., memory allocation error).
 */
int insert_hashmap(HashMap *map, const char *key, void *value);

/**
 * @brief retrieves the value associated with a given key from the hash map.
 * @param map a pointer to the hashmap.
 * @param key the string key to search for.
 * @return a pointer to the stored value, or null if the key is not found.
 */
void *get_hashmap(const HashMap *map, const char *key);

/**
 * @brief deletes a key-value pair from the hash map.
 * @param map a pointer to the hashmap.
 * @param key the string key to delete.
 * @return 0 on success, -1 if the key was not found.
 */
int delete_hashmap(HashMap *map, const char *key);

/**
 * @brief clears all key-value pairs in the hash map.
 * @param map a pointer to the hashmap.
 */
void clear_hashmap(HashMap *map);

/**
 * @brief frees all memory allocated for the hash map.
 * this is crucial to prevent memory leaks.
 * @param map a pointer to the hashmap.
 */
void free_hashmap(HashMap *map);

#endif // HASHMAP_H
