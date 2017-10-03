#!/usr/bin/env perl

print "time\ttput\n";
while (<>) {
  next unless /sec;/;
  next if /CLEANUP/;
  if (/(\d+) sec.*?([\d.]+) current ops./) {
    print "$1\t$2\n";
  }
}
