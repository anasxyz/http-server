#ifndef _UTIL_H_
#define _UTIL_H_

void logs(char type, const char *fmt, const char *extra_fmt, ...);
void exits();
int set_nonblocking(int fd);
int setup_listening_socket(int port);

#endif // _UTIL_H_
