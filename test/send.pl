#!/usr/bin/perl
use strict;
use IO::Socket;
use Data::Dumper;
use Getopt::Long;
use Time::HiRes qw(time nanosleep);
my $file = "";
my $port = 10000;
my $host = "localhost";
my $ns   = 1000;
GetOptions ("port=i"      => \$port,
            "file=s"      => \$file,
            "nanosleep=i" => \$ns,
            "host=s"      => \$host);
die "usage: $0 --port=10000 --host=localhost --file=/tmp/example.txt" unless $file && $port && $host;

open(my $fh,'<',$file);
my $data = do { local $/; <$fh> };
close($fh);

my $i = 0;
my $now = time();
my $remote = IO::Socket::INET->new(Proto => 'udp',PeerAddr => $host,PeerPort => $port) or die $!;
while (1) {
    if (int($now) != int(time())) {
        print "SENDING $i packets per second\n";
        $now = time();
        $i = 0;
    }
    $remote->send($data);
    $i++;
    nanosleep($ns) if $ns;
}
