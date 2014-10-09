#!/usr/bin/env perl

use strict;
use warnings;

use Getopt::Long;
use IO::Select;
use POSIX qw(EWOULDBLOCK EAGAIN F_SETFL F_GETFL O_NONBLOCK);
use Sereal::Decoder qw(sereal_decode_with_object scalar_looks_like_sereal);
use Time::HiRes;
use Socket;

my %Opt =
    (
     port    => 9003,
     sec     => 0,
     forever => 1,
     period  => 60,
    );

die "usage: $0 --port=$Opt{port} [--sec=N|--forever] --period=N"
    unless (GetOptions(
		"port=i"   => \$Opt{port},
		"sec=i"    => \$Opt{sec},
		"forever"  => \$Opt{forever},
		"period"   => \$Opt{period},
	    ));

if ($Opt{sec} > 0) {
    print "NOTE: will exit after sec $Opt{sec}\n";
    $Opt{forever} = 0;
} elsif ($Opt{forever}) {
    print "NOTE: will loop forever\n";
    if ($Opt{sec} > 0) {
	print "NOTE: ignoring sec\n";
    }
}

my $SELECT = IO::Select->new();
socket(my $server, PF_INET, SOCK_STREAM, getprotobyname('tcp'));
setsockopt($server, SOL_SOCKET, SO_REUSEADDR, 1);

my $srl= Sereal::Decoder->new();

my $sa = sockaddr_in($Opt{port}, INADDR_ANY);
bind($server, $sa) or die $!;
set_non_blocking($server);
$SELECT->add($server);
listen($server,SOMAXCONN) or die $!;

my %DATA = ();
my %SIZE = ();
my %HAS_HEADER = ();
my $FULL_HEADER_SIZE = 4;
my @DONE = ();
my $START = Time::HiRes::time();
my $NOW = int($START);
my $TOTAL_PACKETS = 0;
my $IDLE_COUNT = 0;
my $BUSY_COUNT = 0;

$SIG{INT} = sub { print "\n"; show_totals(); exit(1); };

$SIG{ALRM} = sub { show_totals(); alarm($Opt{period}); };
alarm($Opt{period});

while (1) {
    my @ready = $SELECT->can_read(1);
    for my $fh(@ready) {
        my $fn = fileno($fh);
        if ($fh == $server) {
            while (1) {
                my $rc = accept(my $client, $server);
                if (!$rc) {
                    last if ($! == EWOULDBLOCK || $! == EAGAIN);
                    die("accept failed: $!");
                }
                reinit(fileno($client));
                set_non_blocking($client);
                $SELECT->add($client);
            }
        } else {
            if (!$HAS_HEADER{fileno($fh)}) {
                if (read_more_or_cleanup($fh,$FULL_HEADER_SIZE)) {
                    $SIZE{$fn} = unpack('L',$DATA{$fn});
                    $DATA{$fn} = "";
                    $HAS_HEADER{$fn} = 1;
                }
            } else {
                if (read_more_or_cleanup($fh,$SIZE{$fn})) {
                    push @DONE,$DATA{$fn};
                    reinit($fn);
                }
            }
        }
    }
    my $now = time();
    if ($now != $NOW) {
	if ($now > $NOW) {
	    flush_totals($now);
	}
        $NOW = $now;
    }
    last if !$Opt{forever} &&
            ($Opt{sec} > 0 && $now - $START >= $Opt{sec});
}
close($server);

sub flush_totals {
    my $now = shift;
    if ($now > $NOW && @DONE) {
	$BUSY_COUNT++;
	my $done = scalar @DONE;
	$TOTAL_PACKETS += $done;
	printf("RECEIVING packets %d / total %d epoch %d busy %d idle %d\n",
	       $done, $TOTAL_PACKETS, $now, $BUSY_COUNT, $IDLE_COUNT);
        for my $e (@DONE) {
            if (scalar_looks_like_sereal($e)) {
                my $try = sereal_decode_with_object($srl, $e);
		# TODO: fail? count?
            }
        }
	@DONE = ();
    } else {
	$IDLE_COUNT++;
	printf("IDLE %d\n", $now);
    }
}

sub show_totals {
    my $now = time();
    flush_totals($now);
    my $took = $now - $START;
    if ($took > 0) {
	printf("RECEIVED packets %d in %.2f sec at %d packets/s epoch %d busy %d idle %d (%.2f)\n",
	       $TOTAL_PACKETS, $took, $TOTAL_PACKETS / $took,
	       $now, $BUSY_COUNT, $IDLE_COUNT, $BUSY_COUNT / ($BUSY_COUNT + $IDLE_COUNT));
    } else {
	die "$0: took less time than nothing\n";
    }
}

exit(0);

sub set_non_blocking {
    my $flags = fcntl($_[0], F_GETFL, 0) or die "Failed to set fcntl F_GETFL flag: $!";
    fcntl($_[0], F_SETFL, $flags | O_NONBLOCK) or die "Failed to set fcntl O_NONBLOCK flag: $!";
}

sub reinit {
    my $fn = shift;
    $DATA{$fn} = "";
    $SIZE{$fn} = 0;
    $HAS_HEADER{$fn} = 0;
}

sub read_more_or_cleanup {
    my ($fh,$size) = @_;
    my $fn = fileno($fh);
    my $rc = sysread($fh,$DATA{$fn},$size - length($DATA{$fn}), length($DATA{$fn}));
    if (!defined($rc) || $rc == 0) {
        return 
            if ($! == EWOULDBLOCK || $! == EAGAIN);

        delete $DATA{$fn};
        delete $SIZE{$fn};
        delete $HAS_HEADER{$fn};
        close($fh);
        $SELECT->remove($fh);
    }
    return (exists $DATA{fn} && length($DATA{$fn}) == $size);
}
