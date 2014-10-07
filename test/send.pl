#!/usr/bin/perl

use strict;
use warnings;

use Getopt::Long;
use IO::Socket;
use Time::HiRes;

my %Opt =
    (
     file   => "out.srl",
     host   => "localhost",
     port   => 10000,
     waitns => 1000,

     count  => undef,
     mb     => undef,
     sec    => undef,
    );

die "usage: $0 --port=$Opt{port} --file=$Opt{file} [--count=N|--MB=N|--sec=N] --waitns=$Opt{waitns} --host=$Opt{host}"
    unless (GetOptions("port=i"      => \$Opt{port},
		       "file=s"      => \$Opt{file},
		       "count=i"     => \$Opt{count},
		       "MB=f"        => \$Opt{mb},
		       "sec=f"       => \$Opt{sec},
		       "waitns=i"    => \$Opt{waitns},
		       "host=s"      => \$Opt{host})
	    # count<0 means 'forever'
	    && ($Opt{count} != 0 || $Opt{mb} > 0 || $Opt{sec} > 0));

open(my $fh, '<', $Opt{file}) or die qq[$0: failed to open "$Opt{file}" for reading: $!];
my $data = do { local $/; <$fh> };
close($fh);
my $data_mb = length($data) / 1024**2;
printf "DATA %.4f MB\n", $data_mb;

if ($Opt{count} == 0 && $Opt{mb} > 0) {
    $Opt{count} = int($Opt{mb} / $data_mb + 0.5);
    print "$0: setting count to $Opt{count} based on $Opt{mb} MB\n";
}

if ($Opt{count} > 0 && $Opt{sec} > 0) {
    print "$0: either count $Opt{count} OR sec $Opt{sec} will finish\n";
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
		printf("SENDING %d packets/s (%.2f MB/s) epoch %d\n",
		       $sent_packets, $data_mb * $sent_packets / $sent_time, $now);
	    }
	}
	$last_time = $now;
        $last_packets = $packets;
    }
    $remote->send($data);
    $packets++;
    last if ($Opt{count} > 0 && $packets >= $Opt{count}) ||
            ($Opt{sec} > 0 && $now - $start_hires >= $Opt{sec});
    Time::HiRes::nanosleep($Opt{waitns}) if $Opt{waitns};
}

sub show_totals {
    $now_hires = Time::HiRes::time();
    my $took = $now_hires - $start_hires;
    if ($took > 0) {
	printf("\nSENT %d packets (%.2f MB) in %.2f sec at %.2f packets/s (%.2f MB/s) epoch %d\n",
	       $packets, $data_mb * $packets, $took, $packets / $took, $data_mb * $packets / $took, int($now_hires));
    } else {
	die "$0: took less time than nothing\n";
    }
}

show_totals();

exit(0);
