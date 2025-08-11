#include "ssl.h"
#include "server.h" // Assuming this has definitions for VERBOSE_MODE
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void init_openssl() {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
}

SSL_CTX *create_ssl_context() {
  const SSL_METHOD *method = TLS_server_method();
  SSL_CTX *ctx = SSL_CTX_new(method);
  if (!ctx) {
    perror("Unable to create SSL context");
    ERR_print_errors_fp(stderr);
    return NULL;
  }
  return ctx;
}

void configure_context(SSL_CTX *ctx, const char *cert_file,
                       const char *key_file) {
  if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
  }
}

int perform_ssl_handshake(SSL *ssl) {
  int ret = SSL_accept(ssl);
  if (ret <= 0) {
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      return 0; // Handshake is in progress
    } else {
      fprintf(stderr, "SSL Handshake Error: ");
      ERR_print_errors_fp(stderr);
      return -1; // Fatal error
    }
  }
  return 1; // Handshake complete
}

ssize_t ssl_read_wrapper(SSL *ssl, void *buf, size_t num) {
  return SSL_read(ssl, buf, num);
}

ssize_t ssl_write_wrapper(SSL *ssl, const void *buf, size_t num) {
  return SSL_write(ssl, buf, num);
}
