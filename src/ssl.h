#ifndef SSL_HANDLER_H
#define SSL_HANDLER_H

#include <openssl/ssl.h>

// Function to initialize the OpenSSL library once per process.
void init_openssl();

// Function to create a new SSL context for a worker.
SSL_CTX *create_ssl_context();

// Function to load the server's certificate and private key.
void configure_context(SSL_CTX *ctx, const char *cert_file, const char *key_file);

// Function to perform the TLS handshake on a new connection.
int perform_ssl_handshake(SSL *ssl);

// Function to wrap the standard read and write functions with SSL.
ssize_t ssl_read_wrapper(SSL *ssl, void *buf, size_t num);
ssize_t ssl_write_wrapper(SSL *ssl, const void *buf, size_t num);

#endif
