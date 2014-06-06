FILES=src/blob.c src/worker.c src/util.c
RELAY=src/relay.c $(FILES)
CLIENT=src/stress_test_client.c $(FILES)
CC=gcc -O3 -Wall -pthread -g -DMAX_WORKERS=2 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
all:
	mkdir -p bin
	$(CC) -o bin/client $(CLIENT)
	$(CC) -DUSE_SERVER -o bin/server $(CLIENT)
	$(CC) -o bin/relay $(RELAY)

clean:
	rm -f bin/relay* test/sock/*
	
run:
	cd test && ./setup.sh
