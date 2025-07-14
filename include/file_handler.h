#ifndef file_handler_h
#define file_handler_h

void serve_file(int socket, const char *path);
void get_mime_type(const char *path);
const char* clean_path(const char *request_path);

#endif /* file_handler_h */
