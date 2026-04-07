CC      = gcc

CFLAGS  = -Wall -Wextra -g -D_FILE_OFFSET_BITS=64 $(shell pkg-config --cflags fuse3)
LDFLAGS = $(shell pkg-config --libs fuse3) -lpthread

TARGET  = mini_unionfs

all: $(TARGET)

$(TARGET): mini_unionfs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

test: all
	bash test_unionfs.sh

.PHONY: all clean test