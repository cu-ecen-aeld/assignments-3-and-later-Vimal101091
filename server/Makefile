# Use the compiler passed by Buildroot (TARGET_CC). Fallback to gcc if not set.
CC ?= $(TARGET_CC)

CFLAGS += -Wall -Werror -g

TARGET = aesdsocket
SRC = aesdsocket.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
