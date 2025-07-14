#ifndef file_handler_h
#define file_handler_h

void serve_file(int socket, const char *path);
void serve_not_found(int socket);

#endif /* file_handler_h */
