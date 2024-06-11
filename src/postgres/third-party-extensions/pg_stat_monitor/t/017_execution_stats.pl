#!/usr/bin/perl

use strict;
use warnings;
use File::Basename;
use File::Compare;
use File::Copy;
use Text::Trim qw(trim);
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
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

my $col_total_time = "total_exec_time";
my $col_min_time = "min_exec_time";
my $col_max_time = "max_exec_time";
my $col_mean_time = "mean_exec_time";
my $col_stddev_time = "stddev_exec_time";

if ($PGSM::PG_MAJOR_VERSION <= 12)
{
   $col_total_time = "total_time";
   $col_min_time = "min_time";
   $col_max_time = "max_time";
   $col_mean_time = "mean_time";
   $col_stddev_time = "stddev_time";
}

# CREATE EXTENSION and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "CREATE PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Run 'SELECT *** ' two times and dump output to out file
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name LIKE '%pg_stat_monitor%';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name LIKE '%pg_stat_monitor%';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT query, calls, ${col_total_time}, ${col_min_time}, ${col_max_time}, ${col_mean_time}, ${col_stddev_time} FROM pg_stat_monitor;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT FROM PGSM view");
PGSM::append_to_file($stdout);

# Test: ${col_total_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_total_time} = 0) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_total_time} is not 0).");

# Test: ${col_min_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_min_time} = 0) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_min_time} is not 0).");

# Test: ${col_max_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_max_time} = 0) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_max_time} is not 0).");

# Test: ${col_mean_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_mean_time} = 0) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_mean_time} is not 0).");

# Test: ${col_stddev_time} is not 0
#($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_stddev_time} = 0) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'f',"Test: ${col_stddev_time} should not be 0).");

# Test: ${col_total_time}  =  ${col_min_time} + ${col_max_time}
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)) FROM pg_stat_monitor WHERE calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Compare: (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)).");

# Test: ${col_mean_time} = ${col_total_time}/2
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)) FROM pg_stat_monitor WHERE calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)).");

# Test: ${col_stddev_time} = ${col_mean_time} - ${col_min_time}
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)) FROM pg_stat_monitor WHERE calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)).");

# Dump output to out file
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT substr(query, 0,100) AS query, calls, ${col_total_time}, ${col_min_time},${col_max_time},${col_mean_time},${col_stddev_time} FROM pg_stat_monitor ORDER BY query;", extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_debug_file($stdout);

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

# DROP EXTENSION
$stdout = $node->safe_psql('postgres', 'DROP EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "DROP PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
