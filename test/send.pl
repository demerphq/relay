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
my $count;
my $usage= !GetOptions ("port=i"      => \$port,
            "file=s"      => \$file,
            "nanosleep=i" => \$ns,
            "count=i"     => \$count,
            "host=s"      => \$host);
die "usage: $0 --port=10000 --host=localhost --file=/tmp/example.txt" if $usage or !($file && $port && $host);

open(my $fh,'<',$file);
my $data = do { local $/; <$fh> };
close($fh);

my $i = 0;
my $last= 0;
my $start= my $now = time();
my $remote = IO::Socket::INET->new(Proto => 'udp',PeerAddr => $host,PeerPort => $port) or die $!;
while (1) {
    if (int($now) != int(time())) {
        print "SENDING ", $i - $last, " packets per second\n";
        $now = time();
        $last= $i;
    }
    $remote->send($data);
    $i++;
    last if $count and $i >= $count;
    nanosleep($ns) if $ns;
}
$now = time();
printf "Sent %d packets at %.2f packets per second\n", $i, $i / ($now - $start);
