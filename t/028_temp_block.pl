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
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pg_stat_monitor'");
# Set bucket duration to 3600 seconds so bucket doesn't change.
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_bucket_time = 1800");
$node->append_conf('postgresql.conf', "track_io_timing = on");
$node->append_conf('postgresql.conf', "log_temp_files = 0");
$node->append_conf('postgresql.conf', "work_mem = 64kB");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_track_utility = no");

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_statements;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSS Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");
chmod(0640 , $out_filename_with_path)
    or die("unable to set permissions for $out_filename_with_path");

# Create extension and change out file permissions
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
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

($cmdret, $stdout, $stderr) = $node->psql('postgres', ' CREATE TABLE t1(a int);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Create Table t1");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE INDEX idx_t1_a on t1(a);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Create index.");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'INSERT INTO t1 VALUES(generate_series(1,1000000));', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Insert 10000 records.");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'ANALYZE t1;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Analyze t1.");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * FROM t1 AS XX INNER JOIN t1 as TT ON XX.a = TT.a;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Select * from t1.");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'Select substr(query,0,130) as query, calls, rows_retrieved, cpu_user_time, cpu_sys_time, local_blks_hit, local_blks_read, local_blks_dirtied, local_blks_written, temp_blks_read, temp_blks_written from pg_stat_monitor where query Like \'%t1%\' order by query,calls desc;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($dynamic_out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT temp_files, temp_bytes FROM pg_stat_database db;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($dynamic_out_filename_with_path, $stdout . "\n");

# Compare values for query 'SELECT * FROM t1' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'Select PGSM.temp_blks_read != 0 from pg_stat_monitor as PGSM where PGSM.query Like \'%FROM t1%\';', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: shared_blks_hit should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'Select PGSM.temp_blks_written != 0 from pg_stat_monitor as PGSM where PGSM.query Like \'%FROM t1%\';', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: temp_blks_read should not be 0.");

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();

