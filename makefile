CC = gcc
CFLAGS = -Wall -Wextra
TARGET = server

SRC = test.c server.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
