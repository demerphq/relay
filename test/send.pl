#!/usr/bin/perl

use strict;
use IO::Socket;
use Data::Dumper;
use Getopt::Long;
use Time::HiRes;
my $file = "out.srl";
my $port = 10000;
my $host = "localhost";
my $ns   = 1000;
my $count;

die "usage: $0 --port=10000f --host=localhost --file=/tmp/example.txt"
    unless (GetOptions("port=i"      => \$port,
		       "file=s"      => \$file,
		       "nanosleep=i" => \$ns,
		       "count=i"     => \$count,
		       "host=s"      => \$host) &&
	    $count);

open(my $fh,'<',$file);
my $data = do { local $/; <$fh> };
close($fh);
my $MB = length($data) / 1024**2;

my $packets = 0;
my $last_packets = 0;
my $start_hires = Time::HiRes::time();
my $now_hires   = $start_hires;
my $last_time;
my $remote = IO::Socket::INET->new(Proto => 'udp',
				   PeerAddr => $host,
				   PeerPort => $port) or die "$0: $!";
while (1) {
    my $now = time();
    if ($last_time != $now) {
	if ($last_time) {
	    my $sent_packets = $packets - $last_packets;
	    if ($now > $last_time) {
		printf("%d SENDING %.2f packets/sec (%.2f MB/s)\n",
		       $now, $sent_packets,
		       $MB * $sent_packets / ($now - $last_time));
	    }
	}
	$last_time = $now;
        $last_packets = $packets;
    }
    $remote->send($data);
    $packets++;
    last if $count and $packets >= $count;
    Time::HiRes::nanosleep($ns) if $ns;
}
$now_hires = Time::HiRes::time();
my $took = $now_hires - $start_hires;
if ($took > 0) {
    printf("%d SENT %d packets (%.2f MB) at %.2f packets/second (%.2f MB/sec)\n",
	   time(), $packets, $MB * $packets, $packets / $took, $MB * $packets / $took);
} else {
    die "$0: took less time than nothing\n";
}

exit(0);
