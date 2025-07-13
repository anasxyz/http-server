#ifndef client_h
#define client_h

struct Client {
  int socket;
};

void *handle_client(void *args);

#endif /* client_h */
