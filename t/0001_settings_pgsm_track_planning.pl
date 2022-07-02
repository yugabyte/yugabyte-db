#!/usr/bin/perl

use String::Util qw(trim);
use File::Basename;
use File::Compare;
use Test::More;

use lib 't';
use pgsm;

sub check_value
{
    my ($var, $postive, $expected, $val) = @_;
    if ($postive)
    {
        is($expected, $val, "Checking $var...");
    }
    else
    {
      isnt($expected, $val, "Checking $var...");
    }
}

sub do_testing
{
    my ($expected, $postive) = @_;
    my ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', 'SELECT max(total_plan_time) FROM pg_stat_monitor;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my $total_plan_time = trim($stdout);

    ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', 'SELECT max(min_plan_time) FROM pg_stat_monitor;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my $min_plan_time = trim($stdout);

    ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', 'SELECT max(max_plan_time) FROM pg_stat_monitor;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my $max_plan_time = trim($stdout);

    ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', 'SELECT max(mean_plan_time) FROM pg_stat_monitor;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my $mean_plan_time = trim($stdout);

    ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', 'SELECT max(stddev_plan_time) FROM pg_stat_monitor;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my $stddev_plan_time = trim($stdout);

    check_value("pgsm_track_planning", $postive, $expected, $total_plan_time + $min_plan_time + $max_plan_time + $stddev_plan_time);
    return ($cmdret, $stdout, $stderr);
}

sub do_regression
{
    my ($postive, $expected, $set) = @_;
    ($cmdret, $stdout, $stderr) = pgsm_setup_pg_stat_monitor($set);
    ($cmdret, $stdout, $stderr) = pgsm_start_pg;
    ($cmdret, $stdout, $stderr) = pgsm_create_extension;

    ($cmdret, $stdout, $stderr) = do_testing($postive, $expected);
    ok($cmdret == 0, "Checking final result ...");

    ($cmdret, $stdout, $stderr) = pgsm_drop_extension;
    ($cmdret, $stdout, $stderr) = pgsm_stop_pg;
}

($cmdret, $stdout, $stderr) = pgsm_init_pg;
do_regression(1, 0, "pg_stat_monitor.pgsm_track_planning = 'no'");
do_regression(0, 0, "pg_stat_monitor.pgsm_track_planning = 'yes'");

($cmdret, $stdout, $stderr) = done_testing();

