CC = gcc
CFLAGS = -Wall -Wextra -Werror -g
PKG_CFLAGS = $(shell pkg-config --cflags glib-2.0)
PKG_LDFLAGS = $(shell pkg-config --libs glib-2.0)

TARGET = server
SRC = $(wildcard src/*.c)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $(TARGET) $(SRC) $(PKG_LDFLAGS)

clean:
	rm -f $(TARGET)
