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

# Dictionary for expected PGSM columns names on different PG server versions
my %pg_versions_pgsm_columns = ( 15 => "application_name,blk_read_time," .
    "blk_write_time,bucket,bucket_done,bucket_start_time,calls," .
    "client_ip,cmd_type,cmd_type_text,comments,cpu_sys_time,cpu_user_time," .
    "datname,elevel,jit_emission_count,jit_emission_time,jit_functions," .
    "jit_generation_time,jit_inlining_count,jit_inlining_time," .
    "jit_optimization_count,jit_optimization_time," .
    "local_blks_dirtied,local_blks_hit,local_blks_read," .
    "local_blks_written,max_exec_time,max_plan_time,mean_exec_time," .
    "mean_plan_time,message,min_exec_time,min_plan_time,planid," .
    "plans_calls,query,query_plan,queryid,relations,resp_calls," .
    "rows_retrieved,shared_blks_dirtied,shared_blks_hit,shared_blks_read," .
    "shared_blks_written,sqlcode,stddev_exec_time,stddev_plan_time," .
    "temp_blk_read_time,temp_blk_write_time,temp_blks_read,temp_blks_written," .
    "top_query,top_queryid,toplevel,total_exec_time,total_plan_time," .
    "userid,wal_bytes,wal_fpi,wal_records",
 14 => "application_name,blk_read_time," .
    "blk_write_time,bucket,bucket_done,bucket_start_time,calls," .
    "client_ip,cmd_type,cmd_type_text,comments,cpu_sys_time,cpu_user_time," .
    "datname,elevel,local_blks_dirtied,local_blks_hit,local_blks_read," .
    "local_blks_written,max_exec_time,max_plan_time,mean_exec_time," .
    "mean_plan_time,message,min_exec_time,min_plan_time,planid," .
    "plans_calls,query,query_plan,queryid,relations,resp_calls," .
    "rows_retrieved,shared_blks_dirtied,shared_blks_hit,shared_blks_read," .
    "shared_blks_written,sqlcode,stddev_exec_time,stddev_plan_time," .
    "temp_blks_read,temp_blks_written,top_query,top_queryid,toplevel," .
    "total_exec_time,total_plan_time,userid,wal_bytes,wal_fpi,wal_records",
 13 => "application_name,blk_read_time," .
    "blk_write_time,bucket,bucket_done,bucket_start_time,calls," .
    "client_ip,cmd_type,cmd_type_text,comments,cpu_sys_time,cpu_user_time," .
    "datname,elevel,local_blks_dirtied,local_blks_hit,local_blks_read," .
    "local_blks_written,max_exec_time,max_plan_time,mean_exec_time," .
    "mean_plan_time,message,min_exec_time,min_plan_time,planid," .
    "plans_calls,query,query_plan,queryid,relations,resp_calls," .
    "rows_retrieved,shared_blks_dirtied,shared_blks_hit,shared_blks_read," .
    "shared_blks_written,sqlcode,stddev_exec_time,stddev_plan_time," .
    "temp_blks_read,temp_blks_written,top_query,top_queryid,toplevel," .
    "total_exec_time,total_plan_time,userid,wal_bytes,wal_fpi,wal_records",
 12 => "application_name,blk_read_time,blk_write_time,bucket,bucket_done," .
    "bucket_start_time,calls,client_ip,cmd_type,cmd_type_text,comments," .
    "cpu_sys_time,cpu_user_time,datname,elevel,local_blks_dirtied," .
    "local_blks_hit,local_blks_read,local_blks_written,max_time,mean_time," .
    "message,min_time,planid,query,query_plan,queryid,relations,resp_calls," .
    "rows_retrieved,shared_blks_dirtied,shared_blks_hit,shared_blks_read," .
    "shared_blks_written,sqlcode,stddev_time,temp_blks_read,temp_blks_written," .
    "top_query,top_queryid,total_time,userid,wal_bytes,wal_fpi,wal_records"
 );

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
PGSM::append_to_file($stdout . "\n");

# Get PGSM columns names from PGSM installation in server 
($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT column_name  FROM INFORMATION_SCHEMA.COLUMNS WHERE table_name = 'pg_stat_monitor' order by column_name;", extra_params => ['-A', '-R,', '-Ptuples_only=on']);
ok($cmdret == 0, "Get columns names in PGSM installation for PG version $PGSM::PG_MAJOR_VERSION");
PGSM::append_to_file($stdout . "\n");

# Compare PGSM column names in installation to expected column names
ok($stdout eq $pg_versions_pgsm_columns{$PGSM::PG_MAJOR_VERSION}, "Compare supported columns names for PG version $PGSM::PG_MAJOR_VERSION against expected");

# Run Select statement against expected column names
($cmdret, $stdout, $stderr) = $node->psql('postgres', "Select $pg_versions_pgsm_columns{$PGSM::PG_MAJOR_VERSION} from pg_stat_monitor;", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Select statement against expected column names");
PGSM::append_to_file($stdout);

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();
