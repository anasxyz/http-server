/*
 * NOT NEEDED FOR NOW
 *
 */

#include <stdlib.h>
#include <string.h>

#include "../include/route.h"
#include "../include/http.h"
#include "../include/utils_http.h"

Route routes[] = {
  {"GET", "/api/status", handle_status},
  // Add more here
};

const size_t num_routes = sizeof(routes) / sizeof(Route);

HttpResponse* handle_status() {
  char* json_body = "{\"status\": \"ok\"}";
  HttpResponse *response = create_dynamic_response(200, "application/json", json_body, strlen(json_body));
  return response;
}
