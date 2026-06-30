# Makefile for QNX Pulse Exercise
# Build: make
# Clean: make clean

CC = qcc
CFLAGS = -Wall -g

# Targets
TARGETS = pulse_server pulse_client

all: $(TARGETS)

# Server executable
pulse_server: pulse_server.c
	$(CC) $(CFLAGS) -o pulse_server pulse_server.c

# Client executable
pulse_client: pulse_client.c
	$(CC) $(CFLAGS) -o pulse_client pulse_client.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
