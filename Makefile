CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_FILE_OFFSET_BITS=64 -I/usr/local/include/fuse -I/opt/homebrew/include/fuse
LDFLAGS = -L/usr/local/lib -L/opt/homebrew/lib -lfuse

TARGET = mini_unionfs
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
