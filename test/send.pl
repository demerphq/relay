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
     proto  => "udp",
     prefix => undef,
     port   => 10000,
     rate   => undef,
     waitns => 1000,
     period => 60,

     count  => 0,
     mb     => 0,
     sec    => 0,

     forever => 0,
    );

die "usage: $0 --host=$Opt{host} --port=$Opt{port} [--proto=[udp|tcp] [--prefix=...] [--file=$Opt{file}] [--count=N|--MB=N|--sec=N|--forever] [--rate=N] [--waitns=$Opt{waitns}] [--period=N]"
    unless (GetOptions("port=i"      => \$Opt{port},
		       "file=s"      => \$Opt{file},
		       "proto=s"     => \$Opt{proto},
		       "prefix=s"    => \$Opt{prefix},
		       "count=i"     => \$Opt{count},
		       "MB=f"        => \$Opt{mb},
		       "sec=f"       => \$Opt{sec},
		       "forever"     => \$Opt{forever},
		       "period=i"    => \$Opt{period},
		       "rate=i"      => \$Opt{rate},
		       "waitns=i"    => \$Opt{waitns},
		       "host=s"      => \$Opt{host})
	    && ($Opt{count} > 0 || $Opt{mb} > 0 || $Opt{sec} > 0 || $Opt{forever}) && $Opt{proto} =~ /^(?:udp|tcp)/);

open(my $fh, '<', $Opt{file}) or die qq[$0: failed to open "$Opt{file}" for reading: $!];
my $data = do { local $/; <$fh> };
close($fh);
my $data_mb = length($data) / 1024**2;
printf "$0: [$$] NOTE: packet size %.4f MB\n", $data_mb;

if ($Opt{count} == 0 && $Opt{mb} > 0) {
    $Opt{count} = int($Opt{mb} / $data_mb + 0.5);
    print "$0: [$$] NOTE: setting count to $Opt{count} based on $Opt{mb} MB\n";
}

if ($Opt{forever}) {
    print "$0: [$$] NOTE: will loop forever\n";
    if (defined $Opt{count} || defined $Opt{sec}) {
	print "$0: [$$] NOTE: ignoring count and sec\n";
    }
} else {
    if ($Opt{count} > 0) {
	print "$0: [$$] NOTE: will exit after count $Opt{count}\n";
    }
    if ($Opt{count} > 0 && $Opt{sec} > 0) {
	print "$0: [$$] NOTE: will exit after sec $Opt{sec}\n";
    }
    if ($Opt{count} > 0 && $Opt{sec} > 0) {
	print "$0: [$$] NOTE: will exit after EITHER count $Opt{count} OR sec $Opt{sec}\n";
    }
}

my $packets = 0;
my $last_packets = 0;
my $start_hires = Time::HiRes::time();
my $now_hires   = $start_hires;
my $last_time   = 0;
my $last_hires  = 0;

$SIG{INT} = sub { print "\n"; show_totals(); exit(1); };
$SIG{PIPE} = sub { print "$0: [$$] SIGPIPE ($!)\n"; exit(1);  };

$SIG{ALRM} = sub { show_totals(); alarm($Opt{period}); };
alarm($Opt{period});

my $remote = IO::Socket::INET->new(Proto => $Opt{proto},
				   PeerAddr => $Opt{host},
				   PeerPort => $Opt{port}) or die "$0: [$$] $!";

my $last_send_fail = 0;

while (1) {
    my $now_hires = Time::HiRes::time();
    my $now = int($now_hires);
    if ($last_time != $now) {
	if ($last_time) {
	    my $sent_packets = $packets - $last_packets;
	    if ($now > $last_time) {
		my $sent_time = $now - $last_time; # Ever > 1.0?
		printf("$0: [$$] SENDING %d packets/s (%.2f MB/s) epoch %d\n",
		       $sent_packets, $data_mb * $sent_packets / $sent_time, $now);
	    }
	}
	$last_time = $now;
	$last_hires = $now_hires;
        $last_packets = $packets;
    }
    my $prefix = $Opt{prefix} // '';
    if ($Opt{proto} eq 'tcp') {
	$prefix = pack('L', length($prefix) + length($data)) . $prefix;
    }
    unless ($remote->send($prefix . $data)) {
	my $errno = $!;
	my $now = time();
	if ($now > $last_send_fail) {
	    $! = $errno;
	    print "$0: [$$] send: $!\n";
	    $last_send_fail = $now;
	}
    }
    $packets++;
    last if !$Opt{forever} &&
            ($Opt{count} > 0 && $packets >= $Opt{count}) ||
            ($Opt{sec} > 0 && $now - $start_hires >= $Opt{sec});
    if (defined $Opt{rate} && $now_hires > $last_hires) {
	my $rate = ($packets - $last_packets) / ($now_hires - $last_hires);
	if ($rate > $Opt{rate}) {
	    my $d = 1 / $Opt{rate};
	    $d -= $Opt{waitns} / 1e9;
	    if ($d > 0) {
		Time::HiRes::sleep($d);
	    }
	}
    }
    Time::HiRes::nanosleep($Opt{waitns}) if $Opt{waitns};
}

sub show_totals {
    $now_hires = Time::HiRes::time();
    my $took = $now_hires - $start_hires;
    if ($took > 0) {
	printf("$0: [$$] SENT %d packets (%.2f MB) in %.2f sec at %.2f packets/s (%.2f MB/s) epoch %d\n",
	       $packets, $data_mb * $packets, $took, $packets / $took, $data_mb * $packets / $took, int($now_hires));
    } else {
	die "$0: [$$] took less time than nothing\n";
    }
}

show_totals();

exit(0);
