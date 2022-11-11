#!/usr/bin/perl

use strict;
use warnings;
use File::Basename;
use File::Compare;
use File::Copy;
use String::Util qw(trim);
use Test::More;
use lib 't';
use pgsm;

# Get filename and create out file name and dirs where requried
PGSM::setup_files_dir(basename($0));

# Create new PostgreSQL node and do initdb
my $node = PGSM->pgsm_init_pg();
my $pgdata = $node->data_dir;

# Update postgresql.conf to include/load pg_stat_monitor library
open my $conf, '>>', "$pgdata/postgresql.conf";
print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
print $conf "pg_stat_monitor.pgsm_enable_query_plan = 'yes'\n";
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name='pg_stat_monitor.pgsm_enable_query_plan';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE TABLE TBL_0(key text primary key, txt_0 text, value_0 int);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "INSERT INTO TBL_0(key, txt_0, value_0) VALUES('000000', '846930886', 1804289383);", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'UPDATE TBL_0 SET value_0 = 1681692777;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,50) as query, calls, query_plan from pg_stat_monitor order by query,calls;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

# Test: planid is not 0 or empty
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select planid from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
isnt($stdout,'',"Test: planid should not be empty");
ok(length($stdout) == 16, 'Length of planid is 16');

$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_enable_query_plan = 'no'\n");
$node->restart();

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'Drop TABLE TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name='pg_stat_monitor.pgsm_enable_query_plan';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE TABLE TBL_0(key text primary key, txt_0 text, value_0 int);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "INSERT INTO TBL_0(key, txt_0, value_0) VALUES('000000', '846930886', 1804289383);", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'UPDATE TBL_0 SET value_0 = 1681692777;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,50) as query, calls, query_plan from pg_stat_monitor order by query,calls;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

# Test: planid is empty
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select planid from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'',"Test: planid should be empty");
ok(length($stdout) == 0, 'Length of planid is 0');

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# compare the expected and out file
my $compare = PGSM->compare_results();

# Test/check if expected and result/out file match. If Yes, test passes.
is($compare,0,"Compare Files: $PGSM::expected_filename_with_path and $PGSM::out_filename_with_path files.");

# Done testing for this testcase file.
done_testing();
