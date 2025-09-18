#ifndef _CLI_H_
#define _CLI_H_

int kill_server();
int dameonise();
int is_server_running();
int get_total_connections();
void display_status();
void print_usage();
int cli_handler(int argc, char *argv[]);

#endif // _CLI_H_
