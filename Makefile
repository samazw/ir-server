OBJECTS=net.o log.o cec.o irsend.o ir-server.o
HEADERS=net.h log.h cec.h irsend.h

CC=gcc
CFLAGS=-g -Wall -lcec -I/usr/local/include

all: ir-server

clean:
	rm -f *.o ir-server

$(OBJECTS):	$(@:%.o=%.c) $(HEADERS)

ir-server:	$(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o ir-server
