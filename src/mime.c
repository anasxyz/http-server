#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hashmap.h"
#include "mime.h"

static HashMap *mime_map = NULL;

void load_mime_types(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    perror("fopen");
    return;
  }

  mime_map = create_hashmap();
  if (!mime_map) {
    fclose(f);
    return;
  }

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || isspace(line[0])) {
      continue;
    }

    char *saveptr;
    char *mime = strtok_r(line, " \t\n", &saveptr);
    if (!mime) {
      continue;
    }

    char *ext;
    while ((ext = strtok_r(NULL, " \t\n", &saveptr))) {
      if (insert_hashmap(mime_map, ext, mime) != 0) {
        fprintf(stderr, "Failed to insert mime type for extension: %s\n", ext);
      }
    }
  }

  fclose(f);
}

char *get_mime_type(const char *filename) {
  if (!mime_map) {
    return global_config->http->default_type;
  }

  const char *ext = strrchr(filename, '.');
  if (!ext) {
    return global_config->http->default_type;
  }
  ext++;

  char *mime = (char *)get_hashmap(mime_map, ext);

  return mime ? mime : global_config->http->default_type;
}

void free_mime_types() {
  if (mime_map) {
    free_hashmap(mime_map);
    mime_map = NULL;
  }
}
