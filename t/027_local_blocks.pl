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

if ($PGSM::PG_MAJOR_VERSION <= 12)
{
    plan skip_all => "pg_stat_monitor test cases for versions 12 and below.";
}

# CREATE new PostgreSQL node and do initdb
my $node = PGSM->pgsm_init_pg();
my $pgdata = $node->data_dir;

# UPDATE postgresql.conf to include/load pg_stat_monitor library
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pg_stat_monitor'");
# Set bucket duration to 3600 seconds so bucket doesn't change.
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_bucket_time = 1800");
$node->append_conf('postgresql.conf', "track_io_timing = on");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_track_utility = no");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_normalized_query = yes");

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# CREATE EXTENSION and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_statements;', extra_params => ['-a']);
ok($cmdret == 0, "CREATE PGSS EXTENSION");
PGSM::append_to_file($stdout);

# CREATE EXTENSION and change out file permissions
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "CREATE PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Run 'SELECT ****' two times and dump output to out file
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name LIKE '%pg_stat_monitor%';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', ' CREATE Temporary TABLE t1(a int); INSERT INTO t1 VALUES(generate_series(1,10000)); ANALYZE t1; SELECT * FROM t1;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "CREATE Temporary Table and INSERT values in a single session.");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT substr(query,0,130) AS query, calls, rows_retrieved, cpu_user_time, cpu_sys_time, local_blks_hit, local_blks_read, local_blks_dirtied, local_blks_written, temp_blks_read, temp_blks_written FROM pg_stat_monitor WHERE query LIKE \'%t1%\' ORDER BY query,calls DESC;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_debug_file($stdout);

# Compare values for query 'INSERT INTO t1 VALUES(generate_series($1,$2))' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT PGSM.local_blks_hit != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO t1%\';', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: local_blks_hit should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT PGSM.local_blks_dirtied != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO t1%\';', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: local_blks_dirtied should not be 0.");

# Compare values for query 'SELECT * FROM t1' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT PGSM.local_blks_hit != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%FROM t1%\';', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: shared_blks_hit should not be 0.");

# DROP EXTENSION
$stdout = $node->safe_psql('postgres', 'DROP EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "DROP PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
