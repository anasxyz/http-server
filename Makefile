CC = gcc
CFLAGS = -O2

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

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "Building $(TARGET)..."
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $(TARGET) $(SRC) $(PKG_LDFLAGS) $(OPENSSL_LDFLAGS)
	@echo "Build complete."

install: $(TARGET)
	# install binary
	@echo "Installing $(TARGET) to $(BINDIR)..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@cp $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Binary installed to $(DESTDIR)$(BINDIR)/$(TARGET)"

	# install config
	@echo "Setting up configuration in $(SYSCONFDIR)..."
	@mkdir -p $(DESTDIR)$(SYSCONFDIR)
	@cp $(CONFIG_SRC) $(DESTDIR)$(SYSCONFDIR)/$(CONFIG_FILE)
	@echo "Installed default config to $(DESTDIR)$(SYSCONFDIR)/$(CONFIG_FILE)"

	# install mime types
	@echo "Setting up mime types in $(SYSCONFDIR)..."
	@mkdir -p $(DESTDIR)$(SYSCONFDIR)
	@cp $(MIME_SRC) $(DESTDIR)$(SYSCONFDIR)/$(MIME_FILE)
	@echo "Installed default mime types to $(DESTDIR)$(SYSCONFDIR)/$(MIME_FILE)"

uninstall:
	@echo "Removing binary $(TARGET) from $(BINDIR)..."
	@rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Uninstall complete. Configuration files are preserved."

clean:
	@echo "Cleaning build artifacts..."
	@rm -f $(TARGET)
	@echo "Clean complete."
