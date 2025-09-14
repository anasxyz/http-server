#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hashmap.h" // Include your custom hashmap header
#include "mime.h"

// The global hashmap to store MIME types.
static HashMap *mime_map = NULL;

/**
 * @brief Loads MIME types from a specified file into the custom hash map.
 * @param filename The path to the MIME types configuration file.
 */
void load_mime_types(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    perror("fopen");
    return;
  }

  // Create a new hash map.
  mime_map = create_hashmap();
  if (!mime_map) {
    fclose(f);
    return;
  }

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    // Skip comments and empty lines.
    if (line[0] == '#' || isspace(line[0])) {
      continue;
    }

    char *saveptr;
    // Parse the MIME type.
    char *mime = strtok_r(line, " \t\n", &saveptr);
    if (!mime) {
      continue;
    }

    // Parse each file extension associated with the MIME type.
    char *ext;
    while ((ext = strtok_r(NULL, " \t\n", &saveptr))) {
      // Pass the tokens directly; the hashmap handles duplication
      if (insert_hashmap(mime_map, ext, mime) != 0) {
        fprintf(stderr, "Failed to insert mime type for extension: %s\n", ext);
      }
    }
  }

  fclose(f);
}

/**
 * @brief Retrieves the MIME type for a given filename based on its extension.
 * @param filename The name of the file.
 * @return The corresponding MIME type string, or a default type if not found.
 */
char *get_mime_type(const char *filename) {
  if (!mime_map) {
    return global_config->http->default_type;
  }

  // Find the last dot to get the file extension.
  const char *ext = strrchr(filename, '.');
  if (!ext) {
    return global_config->http->default_type;
  }
  ext++; // Skip the dot.

  // Look up the extension in the custom hash map.
  char *mime = (char *)get_hashmap(mime_map, ext);

  // Return the found MIME type or the default.
  return mime ? mime : global_config->http->default_type;
}

/**
 * @brief Frees all memory allocated for the MIME type hash map.
 */
void free_mime_types() {
  // The custom hash map `free_hashmap` function handles freeing all
  // allocated memory for keys, values, and the map structure itself.
  if (mime_map) {
    free_hashmap(mime_map);
    mime_map = NULL;
  }
}
