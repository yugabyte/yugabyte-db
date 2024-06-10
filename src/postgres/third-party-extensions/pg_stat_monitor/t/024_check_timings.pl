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
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pg_stat_statements,pg_stat_monitor'");
# Set bucket duration to 3600 seconds so bucket doesn't change.
$node->append_conf('postgresql.conf', "pg_stat_statements.track_utility = off");
$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_bucket_time = 36000");
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

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_statements_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSS EXTENSION");
PGSM::append_to_file($stdout);

# Run 'SELECT ***' two times and dump output to out file
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT name, setting, unit, context, vartype, source, min_val, max_val, enumvals, boot_val, reset_val, pending_restart FROM pg_settings WHERE name LIKE '%pg_stat_monitor%';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM EXTENSION Settings");
PGSM::append_to_file($stdout);

my $port = $node->port;

my $out = system ("pgbench -i -s 20 -p $port postgres");
ok($cmdret == 0, "Perform pgbench init");

$out = system ("pgbench -c 10 -j 2 -t 2500 -p $port postgres");
ok($cmdret == 0, "Run pgbench");

($cmdret, $stdout, $stderr) = $node->psql('postgres', "DELETE FROM pgbench_accounts WHERE aid % 9 = 1;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
($cmdret, $stdout, $stderr) = $node->psql('postgres', "DELETE FROM pgbench_accounts WHERE aid % 10 = 1;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
($cmdret, $stdout, $stderr) = $node->psql('postgres', "DELETE FROM pgbench_accounts WHERE aid % 5 = 1;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
($cmdret, $stdout, $stderr) = $node->psql('postgres', "DELETE FROM pgbench_accounts WHERE aid % 3 = 1;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
($cmdret, $stdout, $stderr) = $node->psql('postgres', "DELETE FROM pgbench_accounts WHERE aid % 2 = 1;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT substr(query,0,130) AS query, calls, rows, total_exec_time,min_exec_time,max_exec_time,mean_exec_time,stddev_exec_time FROM pg_stat_statements WHERE query LIKE \'%bench%\' ORDER BY query,calls DESC;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_debug_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT substr(query,0,130) AS query, calls, rows, total_exec_time, min_exec_time, max_exec_time, mean_exec_time,stddev_exec_time, cpu_user_time, cpu_sys_time FROM pg_stat_monitor WHERE query LIKE \'%bench%\' ORDER BY query,calls DESC;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
PGSM::append_to_debug_file($stdout);

# Compare values for query 'DELETE FROM pgbench_accounts WHERE $1 = $2' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.total_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: total_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.min_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: min_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.max_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: max_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.mean_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: mean_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.stddev_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: stddev_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_user_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_user_time should not be 0.");
 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_sys_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%DELETE FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_sys_time should not be 0.");

# Compare values for query 'INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES ($1, $2, $3, $4, CURRENT_TIMESTAMP)' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.total_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: total_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.min_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: min_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.max_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: max_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.mean_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: mean_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.stddev_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: stddev_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_user_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_user_time should not be 0.");
 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_sys_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%INSERT INTO pgbench_history%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_sys_time should not be 0.");

# Compare values for query 'SELECT abalance FROM pgbench_accounts WHERE aid = $1' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.total_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: total_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.min_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: min_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.max_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: max_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.mean_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: mean_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.stddev_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: stddev_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_user_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_user_time should not be 0.");
 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_sys_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%SELECT abalance FROM pgbench_accounts%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_sys_time should not be 0.");

# Compare values for query 'UPDATE pgbench_accounts SET abalance = abalance + $1 WHERE aid = $2' 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.total_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: total_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.min_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: min_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.max_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: max_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.mean_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: mean_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.stddev_exec_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: stddev_exec_time should not be 0.");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_user_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_user_time should not be 0.");
 
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT SUM(PGSM.cpu_sys_time) != 0 FROM pg_stat_monitor AS PGSM WHERE PGSM.query LIKE \'%UPDATE pgbench_accounts SET abalance%\' GROUP BY query;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'t',"Check: cpu_sys_time should not be 0.");

# DROP EXTENSION
$stdout = $node->safe_psql('postgres', 'DROP EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "DROP PGSM EXTENSION");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
