#ifndef file_handler_h
#define file_handler_h

#include <stdio.h>

FILE* get_file(const char *path);
char* read_file(FILE *file);

#endif /* file_handler_h */
