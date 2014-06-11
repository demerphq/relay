export RELAY=../bin/clang/relay
killall -9 relay
export FIRST_PORT=10000
export FIRST_PORT_PLUS_ONE=10001
export LAST_PORT=10009
export LISTENER_PORT=9003

####################################################
export THIS_PORT=$FIRST_PORT
export NEXT_PORT=$FIRST_PORT_PLUS_ONE
export PROTO=upd

for NEXT_PORT in $(seq $FIRST_PORT_PLUS_ONE $LAST_PORT)
do
    $RELAY $PROTO@localhost:$THIS_PORT tcp@localhost:$NEXT_PORT &
    THIS_PORT=$NEXT_PORT
    PROTO=tcp
done

$RELAY tcp@localhost:$THIS_PORT tcp@localhost:$LISTENER_PORT &
ps auwx | grep relay
../test/simple-listener.pl
killall relay

