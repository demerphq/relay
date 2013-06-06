#!/bin/sh
S1=tcp@localhost:12366
S2=tcp@localhost:12367
./stop.sh
PORT=10000
cd ../ && make && cd -
for size in 64 128 256 1024 16000 32000 40000 65000; do
	for i in `ls -1 ../bin/relay* | sed -e 's#../bin/##'`; do
		./stop.sh
		PORT=$(($PORT + 2))
		S1="tcp@localhost:$PORT"
		S2="tcp@localhost:$(($PORT + 1))"
		for sock in sock/fallback.sock $S1 $S2; do
			../bin/server $sock 0 $size &
		done
		sleep 1
		CMD="../bin/$i sock/relay.sock sock/fallback.sock $S1 $S2"
		echo $CMD
		# valgrind --tool=callgrind --dump-instr=yes --simulate-cache=yes --collect-jumps=yes $CMD &
		$CMD &
		sleep 1
		../bin/client sock/relay.sock 4000000 $size > result-$size-$i.txt  2>&1
	done
done
