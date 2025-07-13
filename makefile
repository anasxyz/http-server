CC = gcc
CFLAGS = -Wall -Wextra
TARGET = server

# Automatically find all .c files in the current directory
SRC = $(wildcard *.c)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
