#ifndef MIME_H
#define MIME_H

/**
 * @brief loads mime types from a file.
 * @param filename the path to the file containing the mime types.
 */
void load_mime_types(const char *filename);

/**
 * @brief gets the mime type for a given file extension.
 * @param filename the path to the file.
 * @return a pointer to the mime type string, or the default type if not found.
 */
char *get_mime_type(const char *filename);

/**
 * @brief frees all memory allocated for the mime types.
 */
void free_mime_types();

#endif // MIME_H
