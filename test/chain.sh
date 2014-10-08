# Create a chain of relays, $RELAY_COUNT of them.
#
# The first relay listens at UDP port $FIRST_PORT
# and relays to TCP port plus one. The rest of
# the relays are all TCP relays, with ports plus one.
#
# The last relay is connected to a test listener.

export FIRST_PORT=10000
export FIRST_PORT_PLUS_ONE=$(expr $FIRST_PORT + 1)
export RELAY_COUNT=10
export LAST_PORT=$(expr $FIRST_PORT + $RELAY_COUNT - 1)
export LISTENER_PORT=9003

export RELAY=../bin/relay
#export RELAY=../bin/relay.clang
killall -9 relay

if test ! -f $RELAY; then
    echo "$0: No relay $RELAY, aborting."
    exit 1
fi

####################################################

export THIS_PORT=$FIRST_PORT
export PROTO=udp

for NEXT_PORT in $(seq $FIRST_PORT_PLUS_ONE $LAST_PORT)
do
    CMD="$RELAY $PROTO@localhost:$THIS_PORT tcp@localhost:$NEXT_PORT"
    echo "$CMD"
    $CMD &
    THIS_PORT=$NEXT_PORT
    PROTO=tcp
done

CMD="$RELAY tcp@localhost:$THIS_PORT tcp@localhost:$LISTENER_PORT"
echo "$CMD"
$CMD &
ps auwx | grep relay
../test/simple-listener.pl
killall relay
sleep 3

exit 0
