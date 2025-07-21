#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/config.h"

// default values
int PORT = 8080;
char *ROOT = "/var/www/";
Route *ROUTES = NULL;
size_t ROUTES_COUNT = 0;
char *TRY_FILES[] = {"index.htm", "index.html"};
int TRY_FILES_COUNT = sizeof(TRY_FILES) / sizeof(TRY_FILES[0]);

