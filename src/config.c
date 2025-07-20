#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/utils_path.h"

// default values
int SERVER_PORT;
char *WEB_ROOT = NULL;
Route *routes = NULL;
size_t num_routes = 0;

