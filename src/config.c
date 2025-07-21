#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/config.h"

// default values
int PORT = 8080;
int DEFAULT_PROXY_PORT = 8080;
char *ROOT = "/var/www/";

Route *ROUTES = NULL;
size_t ROUTES_COUNT = 0;

char **TRY_FILES;
size_t TRY_FILES_COUNT = 0;

Alias *ALIASES = NULL;
size_t ALIASES_COUNT = 0;

Proxy *PROXIES = NULL;
size_t PROXIES_COUNT = 0;

void load_mock_config() {
  // TRY_FILES
  TRY_FILES_COUNT = 3;
  TRY_FILES = malloc(sizeof(char*) * TRY_FILES_COUNT);
  if (!TRY_FILES) return;
  TRY_FILES[0] = strdup("index.htm");
  TRY_FILES[1] = strdup("index.html");
  TRY_FILES[2] = strdup("404.html");

  // ALIASES
  ALIASES_COUNT = 2;
  ALIASES = malloc(sizeof(Alias) * ALIASES_COUNT);
  if (!ALIASES) return;
  ALIASES[0].from = strdup("/stuff/");
  ALIASES[0].to = strdup("/var/www/stuff/");
  ALIASES[1].from = strdup("/images/");
  ALIASES[1].to = strdup("/var/www/stuff/images/tiger.jpeg");

  // PROXIES
  PROXIES_COUNT = 2;
  PROXIES = malloc(sizeof(Proxy) * PROXIES_COUNT);
  PROXIES[0].from = strdup("/api/");
  PROXIES[0].to = strdup("http://httpbin.org/");
  PROXIES[1].from = strdup("/status/");
  PROXIES[1].to = strdup("http://localhost:5050/");
}

void free_mock_config() {
  // TRY_FILES
  for (size_t i = 0; i < TRY_FILES_COUNT; i++) {
    free(TRY_FILES[i]);
  }
  free(TRY_FILES);

  // ALIASES
  for (size_t i = 0; i < ALIASES_COUNT; i++) {
    free(ALIASES[i].from);
    free(ALIASES[i].to);
  }
  free(ALIASES);

  // PROXIES
  for (size_t i = 0; i < PROXIES_COUNT; i++) {
    free(PROXIES[i].from);
    free(PROXIES[i].to);
  }
  free(PROXIES);
}
