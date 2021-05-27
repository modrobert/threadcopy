CC = gcc
CFLAGS = -O2 -Wpedantic -pthread

default: threadcopy

debug: CFLAGS = -g -Wpedantic -pthread
debug: threadcopy

threadcopy.o: threadcopy.c
	$(CC) $(CFLAGS) -c threadcopy.c -o threadcopy.o

threadcopy: threadcopy.o
	$(CC) $(CFLAGS) threadcopy.o -o threadcopy

clean:
	-rm -f threadcopy.o
	-rm -f threadcopy
	
