#ifndef file_handler_h
#define file_handler_h

void serve_file(int socket, const char *path);
void serve_not_found(int socket);

void get_mime_type(const char *path);
bool does_path_exist(const char *path);
const char* clean_path(const char *path);
const char* get_final_path(const char *path);

#endif /* file_handler_h */
