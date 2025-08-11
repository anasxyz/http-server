CC = gcc
CFLAGS = -Wall -Wextra -Werror -g

# Glib flags
PKG_CFLAGS = $(shell pkg-config --cflags glib-2.0)
PKG_LDFLAGS = $(shell pkg-config --libs glib-2.0)

# OpenSSL flags
OPENSSL_LDFLAGS = -lssl -lcrypto

TARGET = server
SRC = $(wildcard src/*.c)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $(TARGET) $(SRC) $(PKG_LDFLAGS) $(OPENSSL_LDFLAGS)

clean:
	rm -f $(TARGET)
