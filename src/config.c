#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/config.h"

// default values
int PORT = 8080;
char *ROOT = "/var/www/";
Route *routes = NULL;
size_t num_routes = 0;

