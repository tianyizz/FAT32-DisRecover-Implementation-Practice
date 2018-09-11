
CC=gcc
CFLAGS=-Wall -g

BINS=notjustcats


all: $(BINS)

notjustcats:  notjustcats.c
	$(CC) $(CFLAGS) -o notjustcats notjustcats.c


clean:
	rm $(BINS)

.FORCE:
