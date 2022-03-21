#!/usr/bin/perl

# Tap test for PG-291 / eb4087be4e8cce8eb19d893d9a47975dd19039a0 bug fix.
#
# Before the fix, there were scenarios in which pg_stat_monitor could lose
# queries (thus wrong call count/stats) when transitioning to a new bucket.
# 
# The problem before the fix is described below:
# 1. Say current active bucket number is 1.
# 2. QueryID XYZ has call count (calls) = 4.
# 3. QueryID also has a pending query (not yet finished).
# 4. Bucket 1 timer expires, pg_stat_monitor moves to bucket 2.
# 5. pg_stat_monitor moves only the pending QueryID XYZ to bucket 2.
# 6. Remove QueryID XYZ from bucket 1 (all stats, calls=4, etc..).
# 
# Since only the pending QueryID XYZ was moved to bucket 2, all
# previous statistics for the same QueryID would have been incorrectly
# removed from the previous bucket (1).
#
# PG-291 fixes the problem, by keeping finished queries in the previous bucket
# and moving only the pending query to the new bucket.
#
# This test works as follows:
# 1. Set pg_stat_monitor bucket time to 14 seconds.
# 2. Start PostgreSQL, reset pg_stat_monitor view.
# 3. Execute "SELECT pg_sleep(5)" three times in a row.
# It's expected that the first two queries execute and finish in the
# first bucket, then the last query starts in the same bucket but is finished
# in the next bucket.
# We expect a total query count of 3 for this query, and it must exist in two
# buckets. 

use strict;
use warnings;
use PostgresNode;
use Test::More;


# Create new PostgreSQL node and do initdb
my $node = PostgresNode->get_new_node('test');
my $pgdata = $node->data_dir;
$node->dump_info;
$node->init;
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pg_stat_monitor'");
# Set bucket duration to 14 seconds so tests don't take too long.
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_bucket_time = 14");

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");

$node->psql('postgres', "SELECT pg_sleep(5)");
$node->psql('postgres', "SELECT pg_sleep(5)");
$node->psql('postgres', "SELECT pg_sleep(5)");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT bucket, queryid, calls, query FROM pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned']);
ok($cmdret == 0, "Query pg_stat_monitor view");

sub trim { my $s = shift; $s =~ s/^\s+|\s+$//g; return $s };

my $last_bucket = "";
my $bucket_cnt = 0;
my $calls = 0;

my @lines = split /\n/, $stdout;

foreach my $line(@lines) {
    my @tokens = split /\|/, $line;
    my $bucket = trim($tokens[0]);
    my $queryid = trim($tokens[1]);
    my $ncalls = trim($tokens[2]);
    my $query = trim($tokens[3]);

    if ($query =~ "SELECT pg_sleep") {
        $calls += $ncalls;
        if ($bucket_cnt == 0) {
            $bucket_cnt += 1;
            $last_bucket = $bucket;
        } elsif ($bucket != $last_bucket) {
            $bucket_cnt += 1;
            $last_bucket = $bucket;
        }
    }
}

ok($calls == 3, "Check total query count is correct");
ok($bucket_cnt == 2, "Check total bucket count is correct");

# Stop the server
$node->stop;
# Done testing for this testcase file.
done_testing();

