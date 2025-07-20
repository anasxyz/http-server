#ifndef utils_path_h
#define utils_path_h

#include <stddef.h>

char *get_mime_type(const char *path);

char *join_paths(const char *root, const char *path, char *out, size_t out_size);

#endif /* utils_path_h */
