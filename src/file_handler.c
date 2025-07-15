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
char* read_file(FILE *file) {
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *file_buffer = malloc(file_size);
  if (!file_buffer) {
    fclose(file);
    return NULL;
  }

  fread(file_buffer, 1, file_size, file);

  return file_buffer;
}


