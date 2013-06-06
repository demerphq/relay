#!/bin/sh
for i in `ls -1 ../bin/`; do
	killall -9 $i 2>/dev/null
done
rm sock/*.sock
