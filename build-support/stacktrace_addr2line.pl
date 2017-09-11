#!/usr/bin/perl
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
#######################################################################
# This script will convert a stack trace with addresses:
#  0x5fb015 yb::master::Master::Init()
#  0x5c2d38 yb::master::MiniMaster::Ports()
#  0x5c31fa yb::master::MiniMaster::Start()
#  0x58270a yb::MiniCluster::Start()
#  0x57dc71 yb::CreateTableStressTest::SetUp()
# To one with line numbers:
#  0x5fb015 yb::master::Master::Init() at /home/user/code/src/yb/src/master/master.cc:54
#  0x5c2d38 yb::master::MiniMaster::Ports() at /home/user/code/src/yb/src/master/mini_master.cc:52
#  0x5c31fa yb::master::MiniMaster::Start() at /home/user/code/src/yb/src/master/mini_master.cc:33
#  0x58270a yb::MiniCluster::Start() at /home/user/code/src/yb/src/integration-tests/cluster.cc:48
#  0x57dc71 yb::CreateTableStressTest::SetUp() at /home/user/code/src/yb/src/tests/stress-test.cc:61
#
# If the script detects that the output is not symbolized, it will also attempt
# to determine the function names, i.e. it will convert:
#  0x5fb015
#  0x5c2d38
#  0x5c31fa
# To:
#  0x5fb015 yb::master::Master::Init() at /home/user/code/src/yb/src/master/master.cc:54
#  0x5c2d38 yb::master::MiniMaster::Ports() at /home/user/code/src/yb/src/master/mini_master.cc:52
#  0x5c31fa yb::master::MiniMaster::Start() at /home/user/code/src/yb/src/master/mini_master.cc:33
#######################################################################
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
no warnings 'portable';  # Support for 64-bit ints required

use File::Basename;

use constant DEBUG => 0;

if (!@ARGV) {
  die <<EOF
Usage: $0 executable [stack-trace-file]

This script will read addresses from a file containing stack traces and
will convert the addresses that conform to the pattern "  0x123456" to line
numbers by calling addr2line on the provided executable.
If no stack-trace-file is specified, it will take input from stdin.
EOF
}

# el6 and other older systems don't support the -p flag,
# so we do our own "pretty" parsing.
sub parse_addr2line_output($$) {
  defined(my $output = shift) or die;
  defined(my $lookup_func_name = shift) or die;
  my @lines = grep { $_ ne '' } split("\n", $output);
  my $pretty_str = '';
  if ($lookup_func_name) {
    $pretty_str .= ' ' . $lines[0];
  }
  $pretty_str .= ' at ' . $lines[1];
  return $pretty_str;
}

sub to_hex_str($) {
  my $number = shift;
  return sprintf("0x%x", $number)
}

my $binary = shift @ARGV;
if (! -x $binary || ! -r $binary) {
  die "Error: Cannot access executable ($binary)";
}

my $main_bin_dir = dirname($binary) . "/../bin";
my $master_binary = $main_bin_dir . "/yb-master";
my $tserver_binary = $main_bin_dir . "/yb-tserver";

# Cache lookups to speed processing of files with repeated trace addresses.
my %addr2line_map = ();

# Disable stdout buffering
$| = 1;

my @library_offsets;
my $library_offsets_sorted = 0;

# Reading from <ARGV> is magical in Perl.
while (defined(my $input = <ARGV>)) {
  # ExternalMiniCluster tests can produce stack traces prefixed by [m-1], [m-2], [m-3], [ts-1],
  # [ts-2], [ts-3], e.g.
  if ($input =~ /Shared library '(.*)' loaded at address 0x([0-9a-f]+)\s*$/) {
    push(@library_offsets, [$1, hex($2)]);
    $library_offsets_sorted = 0;
  }

  if ($input =~ /^(?:\[([a-z]+)-\d+\])?\s+\@\s+(0x[[:xdigit:]]{6,})(?:\s+(\S+))?/) {
    my $minicluster_daemon_prefix = $1;
    my $addr = $2;
    my $lookup_func_name = (!defined $3);

    my $current_binary = $binary;
    if (defined($minicluster_daemon_prefix)) {
      if ($minicluster_daemon_prefix eq "m") {
        $current_binary = $master_binary;
      } elsif ($minicluster_daemon_prefix eq "ts") {
        $current_binary = $tserver_binary;
      }
    }

    if (!exists($addr2line_map{$addr})) {
      if (!$library_offsets_sorted) {
        @library_offsets = sort { $a->[1] <=> $b->[1] } @library_offsets;
        if (DEBUG) {
          for my $library_entry (@library_offsets) {
            print "Library " . $library_entry->[0] . " is loaded at offset " .
                  to_hex_str($library_entry->[1]) . "\n";;
          }
        }
        $library_offsets_sorted = 1;
      }

      # The following could be optimized with binary search.
      my $parsed_addr = hex($addr);
      my $addr_to_use = $parsed_addr;
      my $binary_to_use = $current_binary;
      for my $library_entry (@library_offsets) {
        my ($library_path, $library_offset) = @$library_entry;
        if ($parsed_addr >= $library_offset) {
          $binary_to_use = $library_path;
          $addr_to_use = $parsed_addr - $library_offset;
        } else {
          # This library and all following libraries are loaded at higher addresses than the address
          # we are looking for.
          last;
        }
      }
      if (DEBUG) {
        print "For input line '$input' address '$addr' appears to be in the binary " .
              "'$binary_to_use'\n";
      }

      my $addr2line_cmd = "addr2line -ifC -e \"" . $binary_to_use . '" ' .
                          to_hex_str($addr_to_use);

      $addr2line_map{$addr} = `$addr2line_cmd`;
    }
    chomp $input;
    $input .= parse_addr2line_output($addr2line_map{$addr}, $lookup_func_name) . "\n";
  }
  print $input;
}

exit 0;
