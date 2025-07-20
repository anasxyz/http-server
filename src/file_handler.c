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
    perror("Failed to open file...\n");
    return NULL;
  }
  
  return file;
}

// reads file into buffer
char *read_file_to_buffer(FILE *fp, size_t *out_size) {
    if (!fp) return NULL;

    const size_t chunk_size = 8192;  // 8 kb chunks
    size_t capacity = chunk_size;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return NULL;

    while (1) {
        if (length + chunk_size > capacity) {
            capacity *= 2;
            char *new_buf = realloc(buffer, capacity);
            if (!new_buf) {
                free(buffer);
                return NULL;
            }
            buffer = new_buf;
        }

        size_t bytes_read = fread(buffer + length, 1, chunk_size, fp);
        length += bytes_read;

        if (bytes_read < chunk_size) {
            if (feof(fp)) break;
            if (ferror(fp)) {
                free(buffer);
                return NULL;
            }
        }
    }

    /*
    // null terminate the buffer for safety if treating as string
    if (length == capacity) {
        char *new_buf = realloc(buffer, capacity + 1);
        if (!new_buf) {
            free(buffer);
            return NULL;
        }
        buffer = new_buf;
    }
    buffer[length] = '\0';
    */

    if (out_size) *out_size = length;
    return buffer;
}

