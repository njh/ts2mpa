CC=gcc
VERSION=0.1
CFLAGS=-g -Wall -DVERSION=$(VERSION)
LDFLAGS=

all: ts2mpa


ts2mpa: ts2mpa.o mpa_header.o
	$(CC) $(LDFLAGS) -o ts2mpa ts2mpa.o mpa_header.o

ts2mpa.o: ts2mpa.c ts2mpa.h mpa_header.h
	$(CC) $(CFLAGS) -c ts2mpa.c

mpa_header.o: mpa_header.c mpa_header.h
	$(CC) $(CFLAGS) -c mpa_header.c
  
clean:
	rm -f *.o ts2mpa
	
dist:
	
	
.PHONY: all clean dist
