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
print $conf "pg_stat_monitor.pgsm_track_planning = 'yes'\n";
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
PGSM::append_to_file($stdout);

# Run 'SELECT * from pg_stat_monitor_settings;' two times and dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name = 'pg_stat_monitor.pgsm_track_planning';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name = 'pg_stat_monitor.pgsm_track_planning';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

SKIP:
   {
      skip "Server version is 12 or less", 1
         if ($PGSM::PG_MAJOR_VERSION <= 12);

      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT query, calls, total_plan_time, min_plan_time, max_plan_time, mean_plan_time, stddev_plan_time from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
      ok($cmdret == 0, "Select from PGSM view");
      PGSM::append_to_file($stdout);

      # Test: total_plan_time is not 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (total_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'f',"Compare: total_plan_time is not 0).");

      # Test: min_plan_time is not 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (min_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'f',"Compare: min_plan_time is not 0).");

      # Test: max_plan_time is not 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (max_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'f',"Compare: max_plan_time is not 0).");

      # Test: mean_plan_time is not 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (mean_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'f',"Compare: mean_plan_time is not 0).");

      # Test: stddev_plan_time is not 0
      #($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (stddev_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      #trim($stdout);
      #is($stdout,'f',"Compare: stddev_plan_time is not 0).");

      # Test: total_plan_time  =  min_plan_time + max_plan_time
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(total_plan_time::numeric,3) = round(min_plan_time::numeric + max_plan_time::numeric,3)) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: (round(total_plan_time::numeric,3) = round(min_plan_time::numeric + max_plan_time::numeric,3)).");
   }

# Test: mean_plan_time = total_plan_time/2
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(mean_plan_time::numeric,3) = round((total_plan_time/2)::numeric,3)) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Test mean_plan_time: (round(mean_plan_time::numeric,3) = round((total_plan_time/2)::numeric,3)).");

# Test: stddev_plan_time = mean_plan_time - min_plan_time
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(stddev_plan_time::numeric,3) = round(mean_plan_time::numeric - min_plan_time::numeric,3)) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare mean_plan_time: (round(stddev_plan_time::numeric,3) = round(mean_plan_time::numeric - min_plan_time::numeric,3)).");

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,100) as query, calls, total_plan_time, min_plan_time,max_plan_time,mean_plan_time,stddev_plan_time from pg_stat_monitor order by query;', extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

# Disable pgsm_track_planning 
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_track_planning = 'no'\n");
$node->restart();

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

# Run 'SELECT * from pg_stat_monitor_settings;' two times and dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name = 'pg_stat_monitor.pgsm_track_planning';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name = 'pg_stat_monitor.pgsm_track_planning';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

SKIP:
   {
      skip "Server version is 12 or less", 1
         if ($PGSM::PG_MAJOR_VERSION <= 12);

      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT query, calls, total_plan_time, min_plan_time, max_plan_time, mean_plan_time, stddev_plan_time from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
      ok($cmdret == 0, "Select from PGSM view");
      PGSM::append_to_file($stdout);

      # Test: total_plan_time is 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (total_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: total_plan_time is 0).");

      # Test: min_plan_time is 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (min_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: min_plan_time is 0).");

      # Test: max_plan_time is 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (max_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: max_plan_time is 0).");

      # Test: mean_plan_time is 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (mean_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: mean_plan_time is 0).");

      # Test: stddev_plan_time is 0
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (stddev_plan_time = 0) from pg_stat_monitor where calls = 2;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
      trim($stdout);
      is($stdout,'t',"Compare: stddev_plan_time is 0).");
   }

# Dump output to out file  
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,100) as query, calls, total_plan_time, min_plan_time,max_plan_time,mean_plan_time,stddev_plan_time from pg_stat_monitor order by query;', extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_file($stdout);

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
