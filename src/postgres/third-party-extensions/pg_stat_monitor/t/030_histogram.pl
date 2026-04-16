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

# Get filename and create out file name and dirs where requried
PGSM::setup_files_dir(basename($0));

# Create new PostgreSQL node and do initdb
my $node = PGSM->pgsm_init_pg();
my $pgdata = $node->data_dir;

my $run_pg_sleep_function_sql = "CREATE OR REPLACE FUNCTION run_pg_sleep(INTEGER) RETURNS VOID AS \$\$
DECLARE
   iterator real := 0.002;  -- we can init with 2 ms at declaration time
   loops ALIAS FOR \$1;
BEGIN
   WHILE iterator < loops
   LOOP
      RAISE INFO 'Current timestamp: %', timeofday()::TIMESTAMP;
      RAISE INFO 'Sleep % seconds', iterator;
	   PERFORM pg_sleep(iterator);
      iterator := iterator + iterator;
   END LOOP;
END;
\$\$ LANGUAGE 'plpgsql' STRICT;";

my $generate_histogram_function_sql = "CREATE OR REPLACE FUNCTION generate_histogram()
    RETURNS TABLE (
    range TEXT, freq INT, bar TEXT
  )  AS \$\$
Declare
    bucket_id integer;
    query_id bigint;
BEGIN
    select bucket into bucket_id from pg_stat_monitor order by calls desc limit 1;
    select queryid into query_id from pg_stat_monitor order by calls desc limit 1;
    return query
    SELECT * FROM histogram(bucket_id, query_id) AS a(range TEXT, freq INT, bar TEXT);
END;
\$\$ LANGUAGE plpgsql;";

sub generate_histogram_with_configurations
{
   my ($h_min, $h_max, $h_buckets, $expected_calls_count, $expected_resp_calls, $expected_histogram_rows, $use_pg_sleep, $scenario_number) = @_;

   PGSM::append_to_debug_file("\n\n===============Scenario $scenario_number - Start===============");

   PGSM::append_to_debug_file("pgsm_histogram_min : " . $h_min);
   PGSM::append_to_debug_file("pgsm_histogram_max : " . $h_max);
   PGSM::append_to_debug_file("pgsm_histogram_buckets : " . $h_buckets);
   PGSM::append_to_debug_file("expected total calls count : " . $expected_calls_count);
   PGSM::append_to_debug_file("expected resp_calls value : " . $expected_resp_calls);
   PGSM::append_to_debug_file("expected ranges (rows) count in histogram: " . $expected_histogram_rows);
   PGSM::append_to_debug_file("using pg_sleep to generate dataset : " . $use_pg_sleep);
   PGSM::append_to_debug_file("scenario number : " . $scenario_number);

   $node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_histogram_min = $h_min");
   $node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_histogram_max = $h_max");
   $node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_histogram_buckets = $h_buckets");
   $node->restart();

   my ($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name LIKE 'pg_stat_monitor%histogram%';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
   ok($cmdret == 0, "Scenario $scenario_number : Print PGSM EXTENSION Settings");
   PGSM::append_to_debug_file($stdout);

   ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
   ok($cmdret == 0, "Scenario $scenario_number : Reset PGSM Extension");
   PGSM::append_to_debug_file($stdout);

   if($use_pg_sleep == 1)
   {
      ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT run_pg_sleep(10);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
      ok($cmdret == 0, "Scenario $scenario_number : Run run_pg_sleep(10) to generate data for histogram testing");
      PGSM::append_to_debug_file($stdout);
   }
   else
   {
      for (my $count = 1 ; $count <= $expected_calls_count ; $count++)
      {
         ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT 1 AS a;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
         ok($cmdret == 0, "Scenario $scenario_number : SELECT 1 AS a");
         PGSM::append_to_debug_file($stdout);
      }
   }

   ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT bucket, queryid, query, calls, resp_calls FROM pg_stat_monitor ORDER BY calls desc;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
   ok($cmdret == 0, "Scenario $scenario_number : Print what is in pg_stat_monitor view");
   PGSM::append_to_debug_file($stdout);

   ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT calls FROM pg_stat_monitor ORDER BY calls desc LIMIT 1;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
   ok($cmdret == 0, "Scenario $scenario_number : Get calls into a variable");
   ok($stdout == $expected_calls_count, "Scenario $scenario_number : Calls are $expected_calls_count");
   my $calls_count = trim($stdout);
   PGSM::append_to_debug_file("Scenario $scenario_number : Got Calls from query : " . $stdout);

   ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT resp_calls FROM pg_stat_monitor ORDER BY calls desc LIMIT 1;', extra_params => ['-Pformat=aligned','-Ptuples_only=on']);
   ok($cmdret == 0, "Scenario $scenario_number : Get resp_calls into a variable");
   ok(trim($stdout) eq "$expected_resp_calls", "Scenario $scenario_number : resp_calls is $expected_resp_calls");
   my $resp_calls = trim($stdout);
   PGSM::append_to_debug_file("Scenario $scenario_number : Got resp_calls from query : " . $stdout);

   ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from generate_histogram();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
   ok($cmdret == 0, "Scenario $scenario_number : Generate Histogram for Select 1");
   PGSM::append_to_debug_file($stdout);
   like($stdout, qr/$expected_histogram_rows rows/, "Scenario $scenario_number : $expected_histogram_rows rows are present in histogram output");

   PGSM::append_to_debug_file("===============End===============");
}

# Update postgresql.conf to include/load pg_stat_monitor library   
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pg_stat_monitor'");

# Set change postgresql.conf for this test case. 
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_bucket_time = 36000");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_normalized_query = yes");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_track = 'all'");

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create functions required for testing
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', "$run_pg_sleep_function_sql", extra_params => ['-a']);
ok($cmdret == 0, "Create run_pg_sleep(INTEGER) function");
PGSM::append_to_debug_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "$generate_histogram_function_sql", extra_params => ['-a']);
ok($cmdret == 0, "Create generate_histogram() function");
PGSM::append_to_debug_file($stdout);

# Create extension and change out file permissions
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
PGSM::append_to_debug_file($stdout);

# Following parameters are required for function 'generate_histogram_with_configurations' to generate and test a histogram
# with given configuration. 
# Parameter 1 ==> pgsm_histogram_min
# Parameter 2 ==> pgsm_histogram_max
# Parameter 3 ==> pgsm_histogram_buckets
# Parameter 4 ==> generated and expected total calls count against 'SELECT 1 AS a' 
# Parameter 5 ==> expected resp_calls value output
# Parameter 6 ==> expected ranges (rows) count in histogram output
# Parameter 7 ==> using pg_sleep to generate dataset, '1' will call sql function 'run_pg_sleep' (declared above).
#                 '0' will call the 'SELECT 1 AS a' expected_total_calls (Parameter 4) times. 
# Parameter 8 ==> Scenario number (placeholder to reflect in log for debugging purposes)

# Scenario 1. Run pg_sleep 
generate_histogram_with_configurations(1, 10000, 3, 13, "{0,4,4,5,0}", 5, 1, 1);

#Scenario 2
generate_histogram_with_configurations(20, 40, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 22, 0, 2);

#Scenario 3
generate_histogram_with_configurations(1000, 1010, 6, 2, "{2,0,0,0,0,0,0,0}", 8, 0, 3);

#Scenario 4
generate_histogram_with_configurations(1, 10, 3, 2, "{2,0,0,0,0}", 5, 0, 4);

#Scenario 5
generate_histogram_with_configurations(0, 2, 2, 2, "{2,0,0}", 3, 0, 5);

#Scenario 6
generate_histogram_with_configurations(1, 2147483647, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 22, 0, 6);

#Scenario 7
generate_histogram_with_configurations(0, 2147483647, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 21, 0, 7);

#Scenario 8
generate_histogram_with_configurations(0, 50000000, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 20, 0, 8);

#Scenario 9
generate_histogram_with_configurations(-1, 2147483648, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 22, 0, 9);

#Scenario 10
generate_histogram_with_configurations(-1, 2147483648, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 22, 0, 10);

#Scenario 11
generate_histogram_with_configurations(0, 49999999, 20, 2, "{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}", 21, 0, 11);

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
PGSM::append_to_debug_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
