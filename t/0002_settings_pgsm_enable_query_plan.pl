#!/usr/bin/perl

use warnings;
use String::Util qw(trim);
use File::Basename;
use File::Compare;
use Test::More;

use lib 't';
use pgsm;

sub check_value
{
    my ($var, $postive, $expected, $val) = @_;
    trim($val);
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
    ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', "select * from pg_class;", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    my ($cmdret, $stdout, $stderr) = pgsm_psql_cmd('postgres', "SELECT case WHEN query_plan LIKE 'Seq Scan on pg_class' THEN  1 ELSE 0 END FROM pg_stat_monitor WHERE query LIKE 'select * from pg_class';", extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
    check_value("query_plan", $postive, $expected, $stdout);
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
do_regression(1, 0, "pg_stat_monitor.pgsm_enable_query_plan = 'no'");
do_regression(1, 1, "pg_stat_monitor.pgsm_enable_query_plan = 'yes'");

($cmdret, $stdout, $stderr) = done_testing();

