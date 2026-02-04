CC ?= gcc
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -pthread -lrt

all: $(TARGET)

default: $(TARGET)

$(TARGET): aesdsocket.o
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

aesdsocket.o: aesdsocket.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o
