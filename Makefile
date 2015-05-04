SASS_LIBSASS_PATH = ./libsass/

CC = gcc
CFLAGS = -Wall `pkg-config --cflags glib-2.0 gio-2.0 libsass`
LDFLAGS = 
LDLIBS = -lstdc++ -lm `pkg-config --libs glib-2.0 gio-2.0 libsass`
SOURCES = gsassc.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = gsassc

all:	libsass $(TARGET)

$(TARGET):	$(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	$(MAKE) -C $(SASS_LIBSASS_PATH) clean

install:
	cp $(TARGET) /usr/local/bin/

.PHONY: clean libsass
.DELETE_ON_ERROR:

