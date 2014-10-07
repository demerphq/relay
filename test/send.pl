#!/usr/bin/perl

use strict;
use IO::Socket;
use Data::Dumper;
use Getopt::Long;
use Time::HiRes;

my %Opt =
    (
     file   => "out.srl",
     count  => undef,
     mb     => undef,
     port   => 10000,
     host   => "localhost",
     waitns => 1000,
    );

die "usage: $0 --port=10000f --host=localhost --file=/tmp/example.txt"
    unless (GetOptions("port=i"      => \$Opt{port},
		       "file=s"      => \$Opt{file},
		       "count=i"     => \$Opt{count},
		       "MB=i"        => \$Opt{mb},
		       "waints=i"    => \$Opt{waitns},
		       "host=s"      => \$Opt{host})
	    && ($Opt{count} || $Opt{mb}));

open(my $fh, '<', $Opt{file}) or die qq[$0: "$Opt{file}": $!];
my $data = do { local $/; <$fh> };
close($fh);
my $data_mb = length($data) / 1024**2;

if ($Opt{count} == 0 && $Opt{mb} > 0) {
    $Opt{count} = int($Opt{mb} / $data_mb + 0.5);
}

my $packets = 0;
my $last_packets = 0;
my $start_hires = Time::HiRes::time();
my $now_hires   = $start_hires;
my $last_time;

$SIG{INT} = sub { show_totals(); exit(1); };

my $remote = IO::Socket::INET->new(Proto => 'udp',
				   PeerAddr => $Opt{host},
				   PeerPort => $Opt{port}) or die "$0: $!";
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
    last if $Opt{count} >= 0 and $packets >= $Opt{count};
    Time::HiRes::nanosleep($Opt{waitns}) if $Opt{waitns};
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
