CC	= gcc
CFLAGS	= -g -O2 -Wno-unused-result
RM	= rm

SERVER	= UDPServer.c
CLIENT	= UDPClient.c
CSRCS	= $(SERVER) $(CLIENT)
TARGETS	= UDPServer UDPClient
OBJECTS	= $(CSRCS:.c=.o)

all:
	make server
	make client

server:
	$(CC) $(CFLAGS) $(SERVER) -o UDPServer

client:
	$(CC) $(CFLAGS) $(CLIENT) -o UDPClient

clean:
	$(RM) -f $(OBJECTS) $(TARGETS)
