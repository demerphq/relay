#!/bin/sh
S1=tcp@localhost:12366
S2=tcp@localhost:12367
./stop.sh
PORT=11000
cd ../ && make && cd -

conf() {
    echo "sock/relay.sock" > /tmp/relay.conf
    echo "sock/fallback.sock" >> /tmp/relay.conf
    echo $1 >> /tmp/relay.conf
    echo $2 >> /tmp/relay.conf
}
conf $S1 $S2
CMD="../bin/relay /tmp/relay.conf"
echo $CMD
# valgrind --tool=callgrind --dump-instr=yes --simulate-cache=yes --collect-jumps=yes $CMD &
$CMD &
for size in 64 128 256 1024 16000 32000 40000 65000; do
        PORT=$(($PORT + 2))
        S1="tcp@localhost:$PORT"
        S2="tcp@localhost:$(($PORT + 1))"
        killall server
        rm sock/fallback.sock
        ../bin/server sock/fallback.sock 0 $size &
        ../bin/server $S1 0 $size &
        ../bin/server $S2 0 $size &
        conf $S1 $S2

        killall -1 relay
        sleep 1
        ../bin/client sock/relay.sock 4000 $size 
#        killall server
done
