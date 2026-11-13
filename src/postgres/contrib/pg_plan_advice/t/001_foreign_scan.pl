# Copyright (c) 2021-2026, PostgreSQL Global Development Group

# Verify plan advice for foreign scans.

use strict;
use warnings FATAL => 'all';

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $node = PostgreSQL::Test::Cluster->new('node');
$node->init;
$node->append_conf('postgresql.conf',
	"session_preload_libraries = 'pg_plan_advice'");
$node->start;

my $host = $node->host;
my $port = $node->port;

$node->safe_psql(
	'postgres', qq{
	CREATE EXTENSION postgres_fdw;
	CREATE SERVER loopback FOREIGN DATA WRAPPER postgres_fdw
		OPTIONS (host '$host', port '$port', dbname 'postgres');
	CREATE USER MAPPING FOR CURRENT_USER SERVER loopback;
	CREATE TABLE base_tab (a int);
	CREATE TABLE base_tab2 (a int);
	CREATE FOREIGN TABLE ftab (a int)
		SERVER loopback OPTIONS (table_name 'base_tab');
	CREATE FOREIGN TABLE ftab2 (a int)
		SERVER loopback OPTIONS (table_name 'base_tab2');
});

# Get the generated advice from EXPLAIN output.
sub extract_generated_advice
{
	my ($explain) = @_;
	my $generated_advice = '';
	my $collecting = 0;
	foreach my $line (split /\n/, $explain)
	{
		if ($line =~ /Generated Plan Advice:/)
		{
			$collecting = 1;
			next;
		}
		if ($collecting)
		{
			$line =~ s/^\s+//;
			$line =~ s/\s+$//;
			$generated_advice .= ' ' if $generated_advice ne '';
			$generated_advice .= $line;
		}
	}
	return $generated_advice;
}

# A pushed-down aggregate over a single foreign table yields a ForeignScan
# that names exactly one relation.  No FOREIGN_JOIN advice should be generated.
my $agg_explain = $node->safe_psql('postgres',
	"EXPLAIN (COSTS OFF, PLAN_ADVICE) SELECT count(*) FROM ftab;");
my $agg_pat = qr/\QRelations: Aggregate on (ftab)\E/;
like($agg_explain, $agg_pat, 'single-table aggregate is pushed down');
my $agg_advice = extract_generated_advice($agg_explain);
is($agg_advice, 'NO_GATHER(ftab)', 'advice for single-table aggregate');

# A foreign join should generate FOREIGN_JOIN advice. Here we force this by
# disabling local join methods.
my $join_explain = $node->safe_psql(
	'postgres', q{
	SET enable_mergejoin = off;
	SET enable_hashjoin = off;
	SET enable_nestloop = off;
	EXPLAIN (COSTS OFF, PLAN_ADVICE) SELECT * FROM ftab JOIN ftab2 USING (a);
});
my $ja_expected = 'FOREIGN_JOIN((ftab ftab2)) NO_GATHER(ftab ftab2)';
my $ja_actual = extract_generated_advice($join_explain);
is($ja_actual, $ja_expected, 'advice for foreign join');

$node->stop;

done_testing();
