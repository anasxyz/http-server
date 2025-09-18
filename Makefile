CC = gcc
CFLAGS = -O2 -g -fno-omit-frame-pointer -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -Wno-unused-result

# Glib flags
PKG_CFLAGS = $(shell pkg-config --cflags glib-2.0)
PKG_LDFLAGS = $(shell pkg-config --libs glib-2.0)
PKG_LDFLAGS += -latomic_ops

# OpenSSL flags
OPENSSL_LDFLAGS = -lssl -lcrypto

TARGET = http-server
SRC = $(wildcard src/*.c)

BINDIR = /usr/local/bin
SYSCONFDIR = /etc/http-server

CONFIG_FILE = http-server.conf
CONFIG_SRC = config/$(CONFIG_FILE)

MIME_FILE = mime.types
MIME_SRC = config/$(MIME_FILE)

PID_FILE = http-server.pid
PID_DEST = /var/run/$(PID_FILE)

LOG_FILE = http-server.log
LOG_DEST = /var/log/http-server/$(LOG_FILE)

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "Building $(TARGET)..."
	@$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $(TARGET) $(SRC) $(PKG_LDFLAGS) $(OPENSSL_LDFLAGS)
	@echo "Build complete."

install: $(TARGET)
# install binary
	@echo "Installing $(TARGET) to $(BINDIR)..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@cp $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)

# install config
	@echo "Setting up configuration in $(SYSCONFDIR)..."
	@mkdir -p $(DESTDIR)$(SYSCONFDIR)
	@cp $(CONFIG_SRC) $(DESTDIR)$(SYSCONFDIR)/$(CONFIG_FILE)

# install mime types
	@echo "Setting up mime types in $(SYSCONFDIR)..."
	@mkdir -p $(DESTDIR)$(SYSCONFDIR)
	@cp $(MIME_SRC) $(DESTDIR)$(SYSCONFDIR)/$(MIME_FILE)

# install PID file
	@echo "Setting up PID file in $(PID_DEST)..."
	@touch $(PID_DEST)
	@chmod 666 $(PID_DEST)

# install log file
	@echo "Setting up log file in $(LOG_DEST)..."
	@touch $(LOG_DEST)
	@chmod 666 $(LOG_DEST)

uninstall:
	@echo "Removing binary $(TARGET) from $(BINDIR)..."
	@rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(TARGET)
	@echo "Clean complete."
