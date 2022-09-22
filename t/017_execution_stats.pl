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

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

# Run 'SELECT * from pg_stat_monitor_settings;' two times and dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT query, calls, ${col_total_time}, ${col_min_time}, ${col_max_time}, ${col_mean_time}, ${col_stddev_time} from pg_stat_monitor;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Select from PGSM view");
PGSM::append_to_file($stdout);

# Test: ${col_total_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_total_time} = 0) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_total_time} is not 0).");

# Test: ${col_min_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_min_time} = 0) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_min_time} is not 0).");

# Test: ${col_max_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_max_time} = 0) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_max_time} is not 0).");

# Test: ${col_mean_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_mean_time} = 0) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_mean_time} is not 0).");

# Test: ${col_stddev_time} is not 0
#($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_stddev_time} = 0) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'f',"Test: ${col_stddev_time} should not be 0).");

# Test: ${col_total_time}  =  ${col_min_time} + ${col_max_time}
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)) from pg_stat_monitor where calls = 2;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Compare: (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)).");

# Test: ${col_mean_time} = ${col_total_time}/2
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)).");

# Test: ${col_stddev_time} = ${col_mean_time} - ${col_min_time}
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)).");

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT substr(query, 0,100) as query, calls, ${col_total_time}, ${col_min_time},${col_max_time},${col_mean_time},${col_stddev_time} from pg_stat_monitor order by query;", extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_debug_file($stdout);

# Dump output to out file  
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
