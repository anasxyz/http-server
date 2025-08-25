#ifndef _MIME_H_
#define _MIME_H_

struct mime_entry {
    char *ext;
    char *mime;
    struct mime_entry *next;
};

void load_mime_types(const char *filename);
const char *get_mime_type(const char *filename);
void free_mime_types();

#endif // _MIME_H_
