#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/file_handler.h"

// reads and serves requested file
FILE* get_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    // perror("Failed to open file...\n");
    return NULL;
  }
  
  return file;
}

// reads file into buffer
char* read_file(FILE* file, size_t* length_out) {
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);

    if (length <= 0) return NULL;  // Ensure length is valid before cast

    size_t size = (size_t) length;

    char* buffer = malloc(size);
    if (!buffer) return NULL;

    size_t read = fread(buffer, 1, size, file);
    fclose(file);

    if (read != size) {
        free(buffer);
        return NULL;
    }

    *length_out = size;
    return buffer;
}


