killall -9 relay
../bin/clang/relay udp@localhost:10000 tcp@localhost:10001 &
../bin/clang/relay tcp@localhost:10001 tcp@localhost:10002 &
../bin/clang/relay tcp@localhost:10002 tcp@localhost:10003 &
../bin/clang/relay tcp@localhost:10003 tcp@localhost:10004 &
../bin/clang/relay tcp@localhost:10004 tcp@localhost:10005 &
../bin/clang/relay tcp@localhost:10005 tcp@localhost:10006 &
../bin/clang/relay tcp@localhost:10006 tcp@localhost:10007 &
../bin/clang/relay tcp@localhost:10007 tcp@localhost:9003 &
./simple-listener.pl
