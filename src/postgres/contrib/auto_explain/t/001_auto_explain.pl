
# Copyright (c) 2021-2022, PostgreSQL Global Development Group

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Runs the specified query and returns the emitted server log.
# params is an optional hash mapping GUC names to values;
# any such settings are transmitted to the backend via PGOPTIONS.
sub query_log
{
	my ($node, $sql, $params) = @_;
	$params ||= {};

	local $ENV{PGOPTIONS} = join " ",
	  map { "-c $_=$params->{$_}" } keys %$params;

	my $log    = $node->logfile();
	my $offset = -s $log;

	$node->safe_psql("postgres", $sql);

	return slurp_file($log, $offset);
}

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init('auth_extra' => [ '--create-role', 'regress_user1' ]);
$node->append_conf('postgresql.conf',
	"session_preload_libraries = 'auto_explain'");
$node->append_conf('postgresql.conf', "auto_explain.log_min_duration = 0");
$node->append_conf('postgresql.conf', "auto_explain.log_analyze = on");
$node->start;

# Simple query.
my $log_contents = query_log($node, "SELECT * FROM pg_class;");

like(
	$log_contents,
	qr/Query Text: SELECT \* FROM pg_class;/,
	"query text logged, text mode");

unlike(
	$log_contents,
	qr/Query Parameters:/,
	"no query parameters logged when none, text mode");

like(
	$log_contents,
	qr/Seq Scan on pg_class/,
	"sequential scan logged, text mode");

# Prepared query.
$log_contents = query_log($node,
	q{PREPARE get_proc(name) AS SELECT * FROM pg_proc WHERE proname = $1; EXECUTE get_proc('int4pl');}
);

like(
	$log_contents,
	qr/Query Text: PREPARE get_proc\(name\) AS SELECT \* FROM pg_proc WHERE proname = \$1;/,
	"prepared query text logged, text mode");

like(
	$log_contents,
	qr/Index Scan using pg_proc_proname_args_nsp_index on pg_proc/,
	"index scan logged, text mode");


# JSON format.
$log_contents = query_log(
	$node,
	"SELECT * FROM pg_class;",
	{ "auto_explain.log_format" => "json" });

like(
	$log_contents,
	qr/"Query Text": "SELECT \* FROM pg_class;"/,
	"query text logged, json mode");

unlike(
	$log_contents,
	qr/"Query Parameters":/,
	"query parameters not logged when none, json mode");

like(
	$log_contents,
	qr/"Node Type": "Seq Scan"[^}]*"Relation Name": "pg_class"/s,
	"sequential scan logged, json mode");

# Prepared query in JSON format.
$log_contents = query_log(
	$node,
	q{PREPARE get_class(name) AS SELECT * FROM pg_class WHERE relname = $1; EXECUTE get_class('pg_class');},
	{ "auto_explain.log_format" => "json" });

like(
	$log_contents,
	qr/"Query Text": "PREPARE get_class\(name\) AS SELECT \* FROM pg_class WHERE relname = \$1;"/,
	"prepared query text logged, json mode");

like(
	$log_contents,
	qr/"Node Type": "Index Scan"[^}]*"Index Name": "pg_class_relname_nsp_index"/s,
	"index scan logged, json mode");

# Check that PGC_SUSET parameters can be set by non-superuser if granted,
# otherwise not

$node->safe_psql(
	"postgres", q{
CREATE USER regress_user1;
GRANT SET ON PARAMETER auto_explain.log_format TO regress_user1;
});

{
	local $ENV{PGUSER} = "regress_user1";

	$log_contents = query_log(
		$node,
		"SELECT * FROM pg_database;",
		{ "auto_explain.log_format" => "json" });

	like(
		$log_contents,
		qr/"Query Text": "SELECT \* FROM pg_database;"/,
		"query text logged, json mode selected by non-superuser");

	$log_contents = query_log(
		$node,
		"SELECT * FROM pg_database;",
		{ "auto_explain.log_level" => "log" });

	like(
		$log_contents,
		qr/WARNING: ( 42501:)? permission denied to set parameter "auto_explain\.log_level"/,
		"permission failure logged");

}    # end queries run as regress_user1

$node->safe_psql(
	"postgres", q{
REVOKE SET ON PARAMETER auto_explain.log_format FROM regress_user1;
DROP USER regress_user1;
});

done_testing();
