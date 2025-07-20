#ifndef file_handler_h
#define file_handler_h

#include <stdio.h>

FILE* get_file(const char *path);
char *read_file_to_buffer(FILE *fp, size_t *out_size);
char *get_body_from_file(const char *filepath);

#endif /* file_handler_h */
