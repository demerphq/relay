#!/usr/bin/env perl

use strict;
use warnings;

use IO::Select;
use POSIX qw(EWOULDBLOCK EAGAIN F_SETFL F_GETFL O_NONBLOCK);
use Sereal::Decoder qw(sereal_decode_with_object scalar_looks_like_sereal);
use Socket;

my $SELECT = IO::Select->new();
socket(my $server, PF_INET, SOCK_STREAM, getprotobyname('tcp'));
setsockopt($server, SOL_SOCKET, SO_REUSEADDR, 1);

my $srl= Sereal::Decoder->new();

my $sa = sockaddr_in(9003, INADDR_ANY);
bind($server, $sa) or die $!;
set_non_blocking($server);
$SELECT->add($server);
listen($server,SOMAXCONN) or die $!;

my %DATA = ();
my %SIZE = ();
my %HAS_HEADER = ();
my $FULL_HEADER_SIZE = 4;
my @DONE = ();
my $NOW = time();
my $total = 0;

while (1) {
    my @ready = $SELECT->can_read(1);
    for my $fh(@ready) {
        my $fn = fileno($fh);
        if ($fh == $server) {
            while (1) {
                my $rc = accept(my $client, $server);
                if (!$rc) {
                    last
                        if ($! == EWOULDBLOCK || $! == EAGAIN);
                    die("accept failed $!");
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
	    my $done = scalar @DONE;
	    $total += $done;
	    printf("packets %d / total %d at %d packets/s epoch %d\n", $done, $total, $done / ($now - $NOW), $now);
	}
        $NOW = $now;
        for my $e (@DONE) {
            if (scalar_looks_like_sereal($e)) {
                my $try = sereal_decode_with_object($srl, $e);
		# TODO: fail? count?
            }
        }
        @DONE = ();
    }
}
close($server);

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
    return (length($DATA{$fn}) == $size);
}

sub set_non_blocking {
    my $flags = fcntl($_[0], F_GETFL, 0) or die "Failed to set fcntl F_GETFL flag: $!";
    fcntl($_[0], F_SETFL, $flags | O_NONBLOCK) or die "Failed to set fcntl O_NONBLOCK flag: $!";
}
