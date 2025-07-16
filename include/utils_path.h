#ifndef utils_path_h
#define utils_path_h

char *get_mime_type(const char *path);
bool does_path_exist(const char *path);
bool is_regular_file(const char *path);
bool is_directory(const char *path);
const char* clean_path(const char *path);
const char* get_full_path(const char *path);
const char* resolve_path(const char* request_path);

#endif /* utils_path_h */
