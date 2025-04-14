
# Copyright (c) 2021-2022, PostgreSQL Global Development Group

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use Test::More;

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init;
$node->start;

$ENV{PGOPTIONS} = '--client-min-messages=WARNING';

$node->issues_sql_like(
	[ 'reindexdb', '-a' ],
	qr/statement: REINDEX.*statement: REINDEX/s,
	'reindex all databases');

$node->safe_psql(
	'postgres', q(
	CREATE DATABASE regression_invalid;
	UPDATE pg_database SET datconnlimit = -2 WHERE datname = 'regression_invalid';
));
$node->command_ok([ 'reindexdb', '-a' ],
  'invalid database not targeted by reindexdb -a');

# Doesn't quite belong here, but don't want to waste time by creating an
# invalid database in 090_reindexdb.pl as well.
$node->command_fails_like([ 'reindexdb', '-d', 'regression_invalid'],
  qr/FATAL:  cannot connect to invalid database "regression_invalid"/,
  'reindexdb cannot target invalid database');

done_testing();
