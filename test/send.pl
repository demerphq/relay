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
my $mb;

die "usage: $0 --port=10000f --host=localhost --file=/tmp/example.txt"
    unless (GetOptions("port=i"      => \$port,
		       "file=s"      => \$file,
		       "nanosleep=i" => \$ns,
		       "count=i"     => \$count,
		       "mb=i"        => \$mb,
		       "host=s"      => \$host) &&
	    ($count || $mb));

open(my $fh,'<',$file);
my $data = do { local $/; <$fh> };
close($fh);
my $data_mb = length($data) / 1024**2;

if ($count == 0 && $mb > 0) {
    $count = int($mb / $data_mb) + 1;
}

my $packets = 0;
my $last_packets = 0;
my $start_hires = Time::HiRes::time();
my $now_hires   = $start_hires;
my $last_time;

$SIG{INT} = sub { show_totals(); exit(1); };

my $remote = IO::Socket::INET->new(Proto => 'udp',
				   PeerAddr => $host,
				   PeerPort => $port) or die "$0: $!";
while (1) {
    my $now = time();
    if ($last_time != $now) {
	if ($last_time) {
	    my $sent_packets = $packets - $last_packets;
	    if ($now > $last_time) {
		my $sent_time = $now - $last_time; # Ever > 1.0?
		printf("SENDING %.2f packets/sec (%.2f data_mb/s) %d\n",
		       $sent_packets, $data_mb * $sent_packets / $sent_time, $now);
	    }
	}
	$last_time = $now;
        $last_packets = $packets;
    }
    $remote->send($data);
    $packets++;
    last if $count >= 0 and $packets >= $count;
    Time::HiRes::nanosleep($ns) if $ns;
}

sub show_totals {
    $now_hires = Time::HiRes::time();
    my $took = $now_hires - $start_hires;
    if ($took > 0) {
	printf("\nSENT %d packets (%.2f MB) in %.2f sec at %.2f packets/second (%.2f MB/sec) %d\n",
	       $packets, $data_mb * $packets, $took, $packets / $took, $data_mb * $packets / $took, int($now_hires));
    } else {
	die "$0: took less time than nothing\n";
    }
}

show_totals();

exit(0);
