# Copyright (c) 2026, PostgreSQL Global Development Group
#
# Test that heap operations clearing visibility map bits (INSERT, UPDATE,
# DELETE, SELECT FOR UPDATE, COPY) correctly register visibility map buffers,
# since incremental backups rely on the WAL summarizer, which only tracks
# registered buffers.

use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

my $tempdir = PostgreSQL::Test::Utils::tempdir_short();
my $mode = $ENV{PG_TEST_PG_COMBINEBACKUP_MODE} || '--copy';

# Set up primary with WAL summarization enabled.
my $primary = PostgreSQL::Test::Cluster->new('primary');
$primary->init(allows_streaming => 1);
$primary->append_conf('postgresql.conf', <<EOF);
summarize_wal = on
autovacuum = off
EOF
$primary->start;

$primary->safe_psql('postgres', q{CREATE EXTENSION pg_visibility});

my @tests = (
	{
		label => 'INSERT',
		table => 'vm_insert_test',
		setup => q{CREATE TABLE vm_insert_test (id int);
			INSERT INTO vm_insert_test DEFAULT VALUES;},
		modify => q{INSERT INTO vm_insert_test VALUES (1)},
		visible_op => '<',
		frozen_op => '<',
	},
	{
		label => 'DELETE',
		table => 'vm_delete_test',
		setup => q{CREATE TABLE vm_delete_test (id int);
			INSERT INTO vm_delete_test VALUES (1), (2);},
		modify => q{DELETE FROM vm_delete_test WHERE id = 1},
		visible_op => '<',
		frozen_op => '<',
	},
	{
		label => 'UPDATE',
		table => 'vm_update_test',
		# Include both same-page and cross-page updates. val is stored PLAIN
		# so the large update stays inline.
		setup => q{CREATE TABLE vm_update_test (id INT, val TEXT);
			ALTER TABLE vm_update_test ALTER COLUMN val SET STORAGE PLAIN;
			INSERT INTO vm_update_test VALUES (1, 'same page'), (2, 'cross page');
			INSERT INTO vm_update_test SELECT i, repeat('a', 200)
				FROM generate_series(3, 70) i;},
		modify => q{UPDATE vm_update_test SET id = 0 WHERE id = 1;
			UPDATE vm_update_test SET val = repeat('b', 4000) WHERE id = 2;},
		# Confirm the small update stays on the same heap page while the large
		# update relocates the tuple to a different heap page.
		ctid_checks => [
			{
				before => 'id = 1',
				after => 'id = 0',
				same => 1,
				desc => 'small update stays on the same heap page',
			},
			{
				before => 'id = 2',
				after => 'id = 2',
				same => 0,
				desc => 'large update moves the tuple to a different heap page',
			},
		],
		visible_op => '<',
		frozen_op => '<',
	},
	{
		label => 'LOCK',
		table => 'vm_lock_test',
		setup => q{CREATE TABLE vm_lock_test (id int);
			INSERT INTO vm_lock_test VALUES (1), (2);},
		modify => q{SELECT * FROM vm_lock_test WHERE id = 1 FOR UPDATE},
		visible_op => '==',
		frozen_op => '<',
	},
	{
		label => 'COPY',
		table => 'vm_copy_test',
		setup => q{CREATE TABLE vm_copy_test (id int);
			INSERT INTO vm_copy_test DEFAULT VALUES;},
		modify => q{COPY vm_copy_test FROM PROGRAM 'echo 42'},
		visible_op => '<',
		frozen_op => '<',
	},
);

sub get_vm_summary
{
	my ($node, $table) = @_;
	my $result = $node->safe_psql('postgres',
		"SELECT all_visible, all_frozen FROM pg_visibility_map_summary('$table')");
	my @vals = split(/\|/, $result);
	return @vals;
}

# Return the heap block number of the (single) row matching $where. The ctid
# is "(block,offset)"; casting it through point lets us pull out the block.
sub heap_block
{
	my ($node, $table, $where) = @_;
	return $node->safe_psql('postgres',
		"SELECT (ctid::text::point)[0]::int FROM $table WHERE $where");
}

# Confirm VACUUM (FREEZE) set VM bits before testing whether later heap
# modifications clear those bits and are captured by incremental backup. We
# could perhaps be more exact than > 0, but the coarseness attempts to avoid
# test flakes.
sub check_vacuumed_vm
{
	my ($node, $test) = @_;
	my ($all_visible, $all_frozen) = get_vm_summary($node, $test->{table});

	cmp_ok($all_visible, '>', 0,
		"$test->{label} test: pages are all-visible after vacuum");
	cmp_ok($all_frozen, '>', 0,
		"$test->{label} test: pages are all-frozen after vacuum");

	return ($all_visible, $all_frozen);
}

# Check the VM bit counts after a heap modification against the post-vacuum
# baseline. Most operations clear both bits; tuple locking clears all-frozen
# without clearing all-visible.
sub check_modified_vm
{
	my ($node, $test) = @_;
	my ($post_visible, $post_frozen) = get_vm_summary($node, $test->{table});

	cmp_ok($post_visible, $test->{visible_op}, $test->{pre_visible},
		"$test->{label} test: all-visible state after modification");
	cmp_ok($post_frozen, $test->{frozen_op}, $test->{pre_frozen},
		"$test->{label} test: all-frozen state after modification");

	return ($post_visible, $post_frozen);
}

# Verify the combined backup restored VM state exactly as it exists on the
# primary, and ask pg_visibility to check that visible tuples are consistent.
sub validate_restored_vm
{
	my ($restored, $test) = @_;

	my ($primary_visible, $primary_frozen) =
	  get_vm_summary($primary, $test->{table});
	my ($restored_visible, $restored_frozen) =
	  get_vm_summary($restored, $test->{table});

	is($restored_visible, $primary_visible,
		"$test->{label} test: restored all_visible count matches primary");
	is($restored_frozen, $primary_frozen,
		"$test->{label} test: restored all_frozen count matches primary");

	my $corrupt_tids = $restored->safe_psql('postgres',
		"SELECT count(*) FROM pg_check_visible('$test->{table}')");
	is($corrupt_tids, '0',
		"$test->{label} test: no VM corruption detected by pg_check_visible");
}

# Create and populate the tables, then vacuum freeze them to set the VM bits
foreach my $test (@tests)
{
	$primary->safe_psql('postgres', $test->{setup});
	$primary->safe_psql('postgres', "VACUUM (FREEZE) $test->{table}");
	($test->{pre_visible}, $test->{pre_frozen}) =
	  check_vacuumed_vm($primary, $test);
}

# Take a full backup
my $full_name = 'full';
my $full_path = $primary->backup_dir . "/$full_name";
$primary->command_ok(
	[
		'pg_basebackup', '--no-sync',
		'--pgdata'      => $full_path,
		'--checkpoint'  => 'fast',
	],
	'full backup');

# Modify the tables and check that the VM bits are as expected for that test
# after the specified modification.
foreach my $test (@tests)
{
	# Record the heap block of any rows whose same-/cross-page movement we
	# want to verify, before the modification relocates them.
	foreach my $check (@{ $test->{ctid_checks} // [] })
	{
		$check->{before_blk} =
		  heap_block($primary, $test->{table}, $check->{before});
	}

	$primary->safe_psql('postgres', $test->{modify});
	($test->{post_visible}, $test->{post_frozen}) =
	  check_modified_vm($primary, $test);

	# Confirm each checked row did (or did not) move to a different heap page.
	foreach my $check (@{ $test->{ctid_checks} // [] })
	{
		my $after_blk = heap_block($primary, $test->{table}, $check->{after});
		if ($check->{same})
		{
			is($after_blk, $check->{before_blk},
				"$test->{label} test: $check->{desc}");
		}
		else
		{
			isnt($after_blk, $check->{before_blk},
				"$test->{label} test: $check->{desc}");
		}
	}
}

# Take an incremental backup. This will have the changes made in the
# modification step.
my $incr_name = 'incr';
my $incr_path = $primary->backup_dir . "/$incr_name";
$primary->command_ok(
	[
		'pg_basebackup', '--no-sync',
		'--pgdata'      => $incr_path,
		'--checkpoint'  => 'fast',
		'--incremental' => $full_path . '/backup_manifest',
	],
	'incremental backup');

# Start a server from a combined backup composed of the incremental and full
# backup.
my $restored = PostgreSQL::Test::Cluster->new('restored');
$restored->init_from_backup($primary, $incr_name,
	combine_with_prior => [$full_name],
	combine_mode       => $mode);
$restored->append_conf('postgresql.conf', <<EOF);
autovacuum = off
EOF
$restored->start;
$restored->safe_psql('postgres', q{CREATE EXTENSION IF NOT EXISTS pg_visibility});

# Confirm that the restored server's visibility map matches the original server
foreach my $test (@tests)
{
	validate_restored_vm($restored, $test);
}

$restored->stop;
$primary->stop;

done_testing();
