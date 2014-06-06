#!/usr/bin/perl
use strict;
use IO::Socket;
use Data::Dumper;
use Getopt::Long;
use Time::HiRes qw(time nanosleep);
my $file = "";
my $port = 10000;
my $host = "localhost";
GetOptions ("port=i" => \$port,
            "file=s" => \$file,
            "host=s" => \$host);
die "usage: $0 --port=1000 --host=localhost --file=/tmp/example.txt" unless $file && $port && $host;

open(my $fh,'<',$file);
my $data = do { local $/; <$fh> };
close($fh);

my $i = 0;
my $t0 = time();
my $every = 1_00_000;
my $remote = IO::Socket::INET->new(Proto => 'udp',PeerAddr => $host,PeerPort => $port) or die $!;
while (1) {
    if ($i++ > $every) {
        my $now = time();
        my $s = ($now - $t0);
        printf "SENDING $i packets took: %.2f s [ %.2f packets per second ]\n", $s,$i/$s;
        $t0 = $now;
        $i = 0;
    }

    $remote->send($data);
}
