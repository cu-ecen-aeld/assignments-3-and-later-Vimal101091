# Use gcc by default, but allow override (for cross-compile)
CC ?= gcc
CFLAGS ?= -Wall -Werror -g

TARGET = aesdsocket
SRC = aesdsocket.c

# Default target
all: $(TARGET)

# Build aesdsocket
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean up
clean:
	rm -f $(TARGET)

.PHONY: all clean
