#!/usr/bin/perl

use strict;
use warnings;
use File::Basename;
use File::Compare;
use File::Copy;
use Test::More;
use lib 't';
use pgsm;

# Get file name and CREATE out file name and dirs WHERE requried
PGSM::setup_files_dir(basename($0));

# CREATE new PostgreSQL node and do initdb
my $node = PGSM->pgsm_init_pg();
my $pgdata = $node->data_dir;

# UPDATE postgresql.conf to include/load pg_stat_monitor library
open my $conf, '>>', "$pgdata/postgresql.conf";
print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
print $conf "pg_stat_monitor.pgsm_bucket_time = 360000\n";
print $conf "pg_stat_monitor.pgsm_query_shared_buffer = 1\n";
print $conf "pg_stat_monitor.pgsm_normalized_query = 'yes'\n";
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# CREATE EXTENSION and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "CREATE PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name='pg_stat_monitor.pgsm_query_shared_buffer';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

# CREATE example database and run pgbench init
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE database example;', extra_params => ['-a']);
print "cmdret $cmdret\n";
ok($cmdret == 0, "CREATE Database example");
PGSM::append_to_file($stdout);

my $port = $node->port;
print "port $port \n";

my $out = system ("pgbench -i -s 10 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Perform pgbench init");

$out = system ("pgbench -c 10 -j 2 -t 1000 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Run pgbench");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT datname, substr(query,0,150) AS query, SUM(calls) AS calls FROM pg_stat_monitor GROUP BY datname, query ORDER BY datname, query, calls DESC Limit 20;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT XXX FROM pg_stat_monitor");
PGSM::append_to_file($stdout);

$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_query_shared_buffer = 2\n");
$node->restart();

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name='pg_stat_monitor.pgsm_query_shared_buffer';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

$out = system ("pgbench -i -s 10 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Perform pgbench init");

$out = system ("pgbench -c 10 -j 2 -t 1000 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Run pgbench");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT datname, substr(query,0,150) AS query, SUM(calls) AS calls FROM pg_stat_monitor GROUP BY datname, query ORDER BY datname, query, calls DESC Limit 20;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT XXX FROM pg_stat_monitor");
PGSM::append_to_file($stdout);

$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_query_shared_buffer = 20\n");
$node->restart();

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name='pg_stat_monitor.pgsm_query_shared_buffer';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

$out = system ("pgbench -i -s 10 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Perform pgbench init");

$out = system ("pgbench -c 10 -j 2 -t 1000 -p $port example");
print " out: $out \n";
ok($cmdret == 0, "Run pgbench");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT datname, substr(query,0,150) AS query, SUM(calls) AS calls FROM pg_stat_monitor GROUP BY datname, query ORDER BY datname, query, calls DESC Limit 20;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT XXX FROM pg_stat_monitor");
PGSM::append_to_file($stdout);

# DROP EXTENSION
$stdout = $node->safe_psql('postgres', 'DROP EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "DROP PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# compare the expected and out file
my $compare = PGSM->compare_results();

# Test/check if expected and result/out file match. If Yes, test passes.
is($compare,0,"Compare Files: $PGSM::expected_filename_with_path and $PGSM::out_filename_with_path files.");

# Done testing for this testcase file.
done_testing();
