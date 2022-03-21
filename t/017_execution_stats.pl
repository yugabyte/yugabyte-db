#!/usr/bin/perl

use strict;
use warnings;
use String::Util qw(trim);
use File::Basename;
use File::Compare;
use PostgresNode;
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

# Create new PostgreSQL node and do initdb
my $node = PostgresNode->get_new_node('test');
my $pgdata = $node->data_dir;
$node->dump_info;
$node->init;

# Update postgresql.conf to include/load pg_stat_monitor library
open my $conf, '>>', "$pgdata/postgresql.conf";
print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

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

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT query, calls, total_exec_time, min_exec_time, max_exec_time, mean_exec_time, stddev_exec_time from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Select from PGSM view");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Test: total_exec_time is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (total_exec_time = 0) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: total_exec_time is not 0).");

# Test: min_exec_time is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (min_exec_time = 0) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: min_exec_time is not 0).");

# Test: max_exec_time is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (max_exec_time = 0) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: max_exec_time is not 0).");

# Test: mean_exec_time is not 0
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (mean_exec_time = 0) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'f',"Compare: mean_exec_time is not 0).");

# Test: stddev_exec_time is not 0
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (stddev_exec_time = 0) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'f',"Test: stddev_exec_time should not be 0).");

# Test: total_exec_time  =  min_exec_time + max_exec_time
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(total_exec_time::numeric,3) = round(min_exec_time::numeric + max_exec_time::numeric,3)) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Compare: (round(total_exec_time::numeric,3) = round(min_exec_time::numeric + max_exec_time::numeric,3)).");

# Test: mean_exec_time = total_exec_time/2
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(mean_exec_time::numeric,3) = round((total_exec_time/2)::numeric,3)) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare mean_exec_time: (round(mean_exec_time::numeric,3) = round((total_exec_time/2)::numeric,3)).");

# Test: stddev_exec_time = mean_exec_time - min_exec_time
#($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select (round(stddev_exec_time::numeric,3) = round(mean_exec_time::numeric - min_exec_time::numeric,3)) from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
#trim($stdout);
#is($stdout,'t',"Compare mean_exec_time: (round(stddev_exec_time::numeric,3) = round(mean_exec_time::numeric - min_exec_time::numeric,3)).");

# Dump output to out file 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,100) as query, calls, total_exec_time, min_exec_time,max_exec_time,mean_exec_time,stddev_exec_time from pg_stat_monitor order by query;', extra_params => ['-a','-Pformat=aligned','-Ptuples_only=off']);
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

# compare the expected and out file
#my $compare = compare($expected_filename_with_path, $out_filename_with_path);

# Test/check if expected and result/out file match. If Yes, test passes.
#is($compare,0,"Compare Files: $expected_filename_with_path and $out_filename_with_path match.");

# Done testing for this testcase file.
done_testing();
