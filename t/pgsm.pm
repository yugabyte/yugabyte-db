package pgsm;

use String::Util qw(trim);
use File::Basename;
use File::Compare;
use PostgresNode;
use Test::More;

our @ISA= qw( Exporter );

# these CAN be exported.
our @EXPORT = qw( pgsm_init_pg pgsm_start_pg pgsm_stop_pg pgsm_psql_cmd pgsm_setup_pg_stat_monitor pgsm_create_extension pgsm_reset_pg_stat_monitor pgsm_drop_extension );
our $pg_node;

# Create new PostgreSQL node and do initdb
sub pgsm_init_pg
{
    $pg_node = PostgresNode->get_new_node('pgsm_regression');
    $pg_node->dump_info;
    $pg_node->init;
}

sub pgsm_start_pg
{
    my $rt_value = $pg_node->start;
    ok($rt_value == 1, "Starting PostgreSQL");
    return $rt_value;
}

sub pgsm_stop_pg
{
  return $pg_node->stop;
}

sub pgsm_psql_cmd
{
    my ($cmdret, $stdout, $stderr) = $pg_node->psql(@_);
}

sub pgsm_setup_pg_stat_monitor
{
    my ($set) = @_;
    my $pgdata = $pg_node->data_dir;
    open my $conf, '>>', "$pgdata/postgresql.conf";
    print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
    print $conf "$set\n";
    close $conf;
}

sub pgsm_create_extension
{
    my ($cmdret, $stdout, $stderr) = $pg_node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
    ok($cmdret == 0, "CREATE EXTENSION pg_stat_monitor...");
    return ($cmdret, $stdout, $stderr);
}

sub pgsm_reset_pg_stat_monitor
{
    # Run required commands/queries and dump output to out file.
    ($cmdret, $stdout, $stderr) = $pg_node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
    ok($cmdret == 0, "Reset pg_stat_monitor...");
    return ($cmdret, $stdout, $stderr);
}

sub pgsm_drop_extension
{
    my ($cmdret, $stdout) = $pg_node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
    ok($cmdret == 0, "DROP EXTENSION pg_stat_monitor...");
    return ($cmdret, $stdout, $stderr);
}
1;

