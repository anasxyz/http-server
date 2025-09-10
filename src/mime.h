#ifndef MIME_H
#define MIME_H

// Function to load MIME types from a file.
// Populates an internal hash map with file extensions and their corresponding
// MIME types.
void load_mime_types(const char *filename);

// Function to get the MIME type for a given filename based on its extension.
// Returns a const char* to the MIME type string or a default value if not
// found.
const char *get_mime_type(const char *filename);

// Function to free all memory associated with the MIME types hash map.
// This should be called on program exit to prevent memory leaks.
void free_mime_types();

#endif // MIME_H
