#!/usr/bin/perl
######################################################################
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
######################################################################
# Tool to parse the output of "debug" refcounted objects.
# This is helpful for tracking down ref-counting leaks.
# Generate compatible logs by making your refcounted object inherit
# from yb::DebugRefCountedThreadSafe<T>.
######################################################################
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
use strict;
use warnings;

my $verbose = 0;
while ($ARGV[0] =~ /^-v/) {
  shift;
  $verbose++;
}
my $infile = shift;

if (!defined $infile || $infile eq '-h' || $infile eq '-help' || $infile eq '--help') {
  die "Usage: $0 [-v] input.log\n";
}

open(FILE, "< $infile") or die "Error: unable to open input file ($infile) for read: $!";
my $content = '';
read(FILE, $content, -s FILE) or die "Error: unable to read input file ($infile): $!";
close FILE;

my @lines = split /\n/, $content;
chomp @lines;

# First, find all the objects we are interested in.
# The really interesting ones have a mismatch in their Inc/Dec counts.

my @incdec = grep { /Incremented ref|Decrementing ref/ } @lines;
my %counts = (); # map of address -> final ref count
foreach my $line (@incdec) {
  if ($line =~ /(Incremented|Decrementing) ref on (0x[a-z0-9]+):/) {
    my $op = $1;
    my $addr = $2;
    if ($op eq 'Incremented') {
      $counts{$addr}++;
    } else {
      $counts{$addr}--;
    }
  }
}

my @bad_addrs = ();
foreach my $addr (sort keys %counts) {
  if ($counts{$addr} != 0) {
    push @bad_addrs, $addr;
  }
}

# Print all the interesting stack traces of the relevant addresses.
foreach my $addr (@bad_addrs) {
  print "Address $addr has bad ref count: " . ($counts{$addr}) . "\n";
  # Parse the stack traces:
  # Find a debug line and grab thru the next two lines that contain "kudu",
  # which should be yb::DebugRefCountedThreadSafe frame through the likely
  # "interesting" frame where the scoped_refptr was instantiated or destroyed.
  my @matches = $content =~ /([^\n]*(?:Incremented|Decrementing) ref on $addr:\n.*?[^\n]+kudu[^\n]+\n.*?[^\n]+kudu[^\n]+\n)/smg;
  foreach (@matches) {
    if ($verbose) {
      # In "verbose" mode, print a slightly larger trace.
      print;
    } else {
      # Regular mode... we just try to get the frame that caused the ref change.
      chomp;
      s/\n.*\n//sm;   # Only keep the first and last lines.
      s/\s+\@\s+/ /;  # Make it look a little nicer.
      print "$_\n";
    }
  }
}
