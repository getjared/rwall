CC = gcc
CFLAGS = -Wall -Wextra -O2 -g `pkg-config --cflags gtk+-3.0 json-glib-1.0 libsoup-2.4`
LIBS = `pkg-config --libs gtk+-3.0 json-glib-1.0 libsoup-2.4`
TARGET = rwall
SRCS = rwall.c
OBJS = $(SRCS:.c=.o)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(BINDIR)/$(TARGET)
