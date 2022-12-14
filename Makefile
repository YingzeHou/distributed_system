CC   = gcc
OPTS = -Wall

server: server.o udp.o
	$(CC) $(OPTS) -c server.c -o server.o -lmfs -L.
	$(CC)  -o server server.o udp.o 

client: client.o udp.o
	$(CC) $(OPTS) -c client.c -o client.o -lmfs -L.
	$(CC)  -o client client.o udp.o -lmfs -L.

mfscli: mfscli.o udp.o
	$(CC) $(OPTS) -c mfscli.c -o mfscli.o -lmfs -L.
	$(CC)  -o mfscli mfscli.o udp.o -lmfs -L.
	
clean: 
	rm -f *.o