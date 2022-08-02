#!/usr/bin/perl

use strict;
use warnings;
use String::Util qw(trim);
use File::Basename;
use File::Compare;
use PostgresNode;
use String::Util qw(trim);
use Test::More;

# Expected folder where expected output will be present
my $expected_folder = "t/expected";

# Results/out folder where generated results files will be placed
my $results_folder = "t/results";

# Check if results folder exists or not, create if it doesn't
unless (-d $results_folder)
{
   mkdir $results_folder or die "Can't create folder $results_folder: $!\n";;
}

# Check if expected folder exists or not, bail out if it doesn't
unless (-d $expected_folder)
{
   BAIL_OUT "Expected files folder $expected_folder doesn't exist: \n";;
}

# Get filename of the this perl file
my $perlfilename = basename($0);

#Remove .pl from filename and store in a variable
$perlfilename =~ s/\.[^.]+$//;
my $filename_without_extension = $perlfilename;

# Create new PostgreSQL node and do initdb
my $node = PostgresNode->get_new_node('test');
my $pgdata = $node->data_dir;
$node->dump_info;
$node->init;

# PG's major server version
open my $FH_PG_VERSION, '<', "${pgdata}/PG_VERSION";
my $major_version = trim(<$FH_PG_VERSION>);
close $FH_PG_VERSION;

# Create expected filename with path
my $expected_filename = "${filename_without_extension}.out";
my $expected_filename_with_path = "${expected_folder}/${expected_filename}" ;

# Create results filename with path
my $out_filename = "${filename_without_extension}.out";
my $out_filename_with_path = "${results_folder}/${out_filename}" ;
my $dynamic_out_filename_with_path = "${results_folder}/${out_filename}.dynamic" ;

# Delete already existing result out file, if it exists.
if ( -f $out_filename_with_path)
{
   unlink($out_filename_with_path) or die "Can't delete already existing $out_filename_with_path: $!\n";
}

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

if ($major_version <= 12)
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
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");
chmod(0640 , $out_filename_with_path)
    or die("unable to set permissions for $out_filename_with_path");

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Run 'SELECT * from pg_stat_monitor_settings;' two times and dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT query, calls, ${col_total_time}, ${col_min_time}, ${col_max_time}, ${col_mean_time}, ${col_stddev_time} from pg_stat_monitor;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Select from PGSM view");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Test: ${col_total_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_total_time} = 0) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_total_time} is not 0).");

# Test: ${col_min_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_min_time} = 0) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_min_time} is not 0).");

# Test: ${col_max_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_max_time} = 0) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_max_time} is not 0).");

# Test: ${col_mean_time} is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_mean_time} = 0) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: ${col_mean_time} is not 0).");

# Test: ${col_stddev_time} is not 0
#($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (${col_stddev_time} = 0) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'f',"Test: ${col_stddev_time} should not be 0).");

# Test: ${col_total_time}  =  ${col_min_time} + ${col_max_time}
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)) from pg_stat_monitor where calls = 2 ;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Compare: (round(${col_total_time}::numeric,3) = round(${col_min_time}::numeric + ${col_max_time}::numeric,3)).");

# Test: ${col_mean_time} = ${col_total_time}/2
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_mean_time}::numeric,3) = round((${col_total_time}/2)::numeric,3)).");

# Test: ${col_stddev_time} = ${col_mean_time} - ${col_min_time}
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare ${col_mean_time}: (round(${col_stddev_time}::numeric,3) = round(${col_mean_time}::numeric - ${col_min_time}::numeric,3)).");

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT substr(query, 0,100) as query, calls, ${col_total_time}, ${col_min_time},${col_max_time},${col_mean_time},${col_stddev_time} from pg_stat_monitor order by query;", extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($dynamic_out_filename_with_path, $stdout . "\n");

# Dump output to out file  
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
