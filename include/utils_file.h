#ifndef utils_file_h
#define utils_file_h

#include <stdio.h>

FILE* get_file(const char *path);
char *read_file_to_buffer(FILE *fp, size_t *out_size);
char *get_body_from_file(const char *path, size_t *out_size);
bool is_dir(const char *path);
bool is_file(const char *path);

#endif /* utils_file_h */
