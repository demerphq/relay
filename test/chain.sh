export RELAY=../bin/clang/relay
killall -9 relay
export THIS_PORT=10000
export NEXT_PORT=10001
export LAST_PORT=9003
export PROTO=upd

for NEXT_PORT in {10001..10001}
do
    $RELAY $PROTO@localhost:$THIS_PORT tcp@localhost:$NEXT_PORT &
    THIS_PORT=$NEXT_PORT
    PROTO=tcp
done

$RELAY tcp@localhost:$THIS_PORT tcp@localhost:$LAST_PORT &
ps auwx | grep relay
../test/simple-listener.pl
killall relay

