
# Copyright (c) 2021-2022, PostgreSQL Global Development Group

use strict;
use warnings;

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Tests to check connection string handling in utilities

# We're going to use byte sequences that aren't valid UTF-8 strings.  Use
# LATIN1, which accepts any byte and has a conversion from each byte to UTF-8.
$ENV{LC_ALL}           = 'C';
$ENV{PGCLIENTENCODING} = 'LATIN1';

# Create database names covering the range of LATIN1 characters and
# run the utilities' --all options over them.
my $dbname1 = generate_ascii_string(1, 63);    # contains '='
my $dbname2 =
  generate_ascii_string(67, 129);    # skip 64-66 to keep length to 62
my $dbname3 = generate_ascii_string(130, 192);
my $dbname4 = generate_ascii_string(193, 255);

my $node = PostgreSQL::Test::Cluster->new('main');
$node->init(extra => [ '--locale=C', '--encoding=LATIN1' ]);
$node->start;

foreach my $dbname ($dbname1, $dbname2, $dbname3, $dbname4, 'CamelCase')
{
	$node->run_log([ 'createdb', $dbname ]);
}

$node->command_ok(
	[qw(vacuumdb --all --echo --analyze-only)],
	'vacuumdb --all with unusual database names');
$node->command_ok([qw(reindexdb --all --echo)],
	'reindexdb --all with unusual database names');
$node->command_ok(
	[qw(clusterdb --all --echo --verbose)],
	'clusterdb --all with unusual database names');

done_testing();
