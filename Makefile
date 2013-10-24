SASS_LIBSASS_PATH = ./libsass/

CC = gcc
CFLAGS = -Wall -I $(SASS_LIBSASS_PATH) `pkg-config --cflags glib-2.0 gio-2.0`
LDFLAGS = 
LDLIBS = -lstdc++ -lm `pkg-config --libs glib-2.0 gio-2.0`
SOURCES = gsassc.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = gsassc


all:	libsass $(TARGET)

$(TARGET):	$(OBJECTS) $(SASS_LIBSASS_PATH)/libsass.a
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SASS_LIBSASS_PATH)/libsass.a: libsass
libsass:
	$(MAKE) -C $(SASS_LIBSASS_PATH)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	$(MAKE) -C $(SASS_LIBSASS_PATH) clean

install:
	cp $(TARGET) /usr/local/bin/

.PHONY: clean libsass
.DELETE_ON_ERROR:

