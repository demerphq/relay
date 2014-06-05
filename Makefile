FILES=src/blob.c src/worker.c src/util.c src/throttle.c
RELAY=src/relay.c $(FILES)
CLIENT=src/client.c $(FILES)
CC=gcc -O3 -Wall -pthread -g -DMAX_WORKERS=2
all:
	mkdir -p bin
	$(CC) -o bin/client $(CLIENT)
	$(CC) -DUSE_SERVER -o bin/server $(CLIENT)

	$(CC) -o bin/relay $(RELAY)
	$(CC) -DUSE_GARBAGE -o bin/relay-ga $(RELAY)
	$(CC) -DUSE_GARBAGE -DUSE_POLLING -o bin/relay-ga-po $(RELAY)

	$(CC) -DUSE_POLLING -o bin/relay-po $(RELAY)
	$(CC) -DUSE_POLLING -DUSE_SPINLOCK -o bin/relay-po-sp $(RELAY)
	$(CC) -DUSE_POLLING -DUSE_SPINLOCK -DUSE_GARBAGE -o bin/relay-po-sp-ga $(RELAY)

clean:
	rm -f bin/relay* test/sock/*
	
run:
	cd test && ./setup.sh
