#ifndef file_handler_h
#define file_handler_h

#include <stdio.h>

FILE* get_file(const char *path);
const char* read_file(FILE *file);

#endif /* file_handler_h */
