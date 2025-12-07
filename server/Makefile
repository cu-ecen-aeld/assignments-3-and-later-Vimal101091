CC ?= $(TARGET_CC)

CFLAGS += -Wall -Werror -g -pthread
LDFLAGS ?=

TARGET = aesdsocket
SRC = aesdsocket.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
