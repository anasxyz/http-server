#ifndef utils_path_h
#define utils_path_h

char *get_mime_type(const char *path);
bool does_path_exist(char *path);
bool is_regular_file(char *path);
bool is_directory(char *path);
char* clean_path(char *path);
char* get_full_path(char *path);
char* resolve_path(char* path);
char *path_pipeline(char *path);

#endif /* utils_path_h */
