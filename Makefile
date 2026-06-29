CC = gcc
CFLAGS = -O2
LIBS = -lcrypt
TARGET = kamputa
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): kamputa.c
	$(CC) $(CFLAGS) kamputa.c -o $(TARGET) $(LIBS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)/etc
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	cp kamputa.conf $(DESTDIR)/etc/kamputa.conf
	chown root:root $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chmod 4755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	chown root:root $(DESTDIR)/etc/kamputa.conf
	chmod 644 $(DESTDIR)/etc/kamputa.conf

clean:
	rm -f $(TARGET)