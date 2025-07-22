#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../include/config.h"

int PORT = 8080;
char *ROOT = NULL;
char **INDEX_FILES = NULL;
size_t INDEX_FILES_COUNT = 0;

char **TRY_FILES = NULL;
size_t TRY_FILES_COUNT = 0;

Alias *ALIASES = NULL;
size_t ALIASES_COUNT = 0;

Proxy *PROXIES = NULL;
size_t PROXIES_COUNT = 0;

void trim(char *str) {
  char *end;

  while (isspace((unsigned char)*str)) str++;

  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  *(end + 1) = '\0';
}

void add_token(char ***list, size_t *count, const char *token) {
  *list = realloc(*list, sizeof(char *) * (*count + 1));
  (*list)[(*count)++] = strdup(token);
}

void load_config(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    perror("Failed to open config file");
    exit(1);
  }

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    trim(line);
    if (line[0] == '#' || strlen(line) == 0)
      continue;

    char *key = strtok(line, " ");
    if (!key) continue;

    if (strcmp(key, "port") == 0) {
      char *val = strtok(NULL, " ");
      if (val) PORT = atoi(val);
    }

    else if (strcmp(key, "root") == 0) {
      char *val = strtok(NULL, " ");
      if (val) ROOT = strdup(val);
    }

    else if (strcmp(key, "index") == 0) {
      char *val;
      while ((val = strtok(NULL, " "))) {
        INDEX_FILES = realloc(INDEX_FILES, sizeof(char *) * (INDEX_FILES_COUNT + 1));
        INDEX_FILES[INDEX_FILES_COUNT++] = strdup(val);
      }
    }

    else if (strcmp(key, "try_files") == 0) {
      char *val;
      while ((val = strtok(NULL, " "))) {
        TRY_FILES = realloc(TRY_FILES, sizeof(char *) * (TRY_FILES_COUNT + 1));
        TRY_FILES[TRY_FILES_COUNT++] = strdup(val);
      }
    }

    else if (strcmp(key, "alias") == 0) {
      char *from = strtok(NULL, " ");
      char *to = strtok(NULL, " ");
      if (from && to) {
        ALIASES = realloc(ALIASES, sizeof(Alias) * (ALIASES_COUNT + 1));
        ALIASES[ALIASES_COUNT].from = strdup(from);
        ALIASES[ALIASES_COUNT].to = strdup(to);
        ALIASES_COUNT++;
      }
    }

    else if (strcmp(key, "proxy") == 0) {
      char *from = strtok(NULL, " ");
      char *to = strtok(NULL, " ");
      if (from && to) {
        PROXIES = realloc(PROXIES, sizeof(Proxy) * (PROXIES_COUNT + 1));
        PROXIES[PROXIES_COUNT].from = strdup(from);
        PROXIES[PROXIES_COUNT].to = strdup(to);
        PROXIES_COUNT++;
      }
    }
  }

  fclose(fp);

  // Print final parsed config for debugging
  printf("----- Loaded Config -----\n");
  printf("Port: %d\n", PORT);
  printf("Root: %s\n", ROOT ? ROOT : "(none)");

  printf("Index Files:\n");
  for (size_t i = 0; i < INDEX_FILES_COUNT; i++)
    printf("  - %s\n", INDEX_FILES[i]);

  printf("Try Files:\n");
  for (size_t i = 0; i < TRY_FILES_COUNT; i++)
    printf("  - %s\n", TRY_FILES[i]);

  printf("Aliases:\n");
  for (size_t i = 0; i < ALIASES_COUNT; i++)
    printf("  - %s => %s\n", ALIASES[i].from, ALIASES[i].to);

  printf("Proxies:\n");
  for (size_t i = 0; i < PROXIES_COUNT; i++)
    printf("  - %s => %s\n", PROXIES[i].from, PROXIES[i].to);

  printf("-------------------------\n");
}

void free_config() {
  for (size_t i = 0; i < INDEX_FILES_COUNT; i++)
    free(INDEX_FILES[i]);
  free(INDEX_FILES);

  for (size_t i = 0; i < TRY_FILES_COUNT; i++)
    free(TRY_FILES[i]);
  free(TRY_FILES);

  for (size_t i = 0; i < ALIASES_COUNT; i++) {
    free(ALIASES[i].from);
    free(ALIASES[i].to);
  }
  free(ALIASES);

  for (size_t i = 0; i < PROXIES_COUNT; i++) {
    free(PROXIES[i].from);
    free(PROXIES[i].to);
  }
  free(PROXIES);

  free(ROOT);
}
