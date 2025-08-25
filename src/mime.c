#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mime.h"

static GHashTable *mime_map = NULL;

void load_mime_types(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    perror("fopen");
    return;
  }

  mime_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || isspace(line[0]))
      continue; // skip comments/empty

    char *saveptr;
    char *mime = strtok_r(line, " \t\n", &saveptr);
    if (!mime)
      continue;

    char *ext;
    while ((ext = strtok_r(NULL, " \t\n", &saveptr))) {
      g_hash_table_insert(mime_map, g_strdup(ext), g_strdup(mime));
    }
  }

  fclose(f);
}

const char *get_mime_type(const char *filename) {
  const char *ext = strrchr(filename, '.');
  if (!ext)
    return global_config->http->default_type;
  ext++; // skip the dot

  const char *mime = g_hash_table_lookup(mime_map, ext);
  return mime ? mime : global_config->http->default_type;
}

void free_mime_types() {
  if (mime_map) {
    g_hash_table_destroy(mime_map);
  }
}
