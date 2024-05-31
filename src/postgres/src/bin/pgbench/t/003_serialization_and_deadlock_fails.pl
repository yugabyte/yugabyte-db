use strict;
use warnings;

use Config;
use PostgresNode;
use TestLib;
use Test::More tests => 34;

use constant
{
	READ_COMMITTED   => 0,
	REPEATABLE_READ  => 1,
	SERIALIZABLE     => 2,
};

my @isolation_level_shell = (
	'read\\ committed',
	'repeatable\\ read',
	'serializable');

# The keys of advisory locks for testing deadlock failures:
use constant
{
	DEADLOCK_1         => 3,
	WAIT_PGBENCH_2     => 4,
	DEADLOCK_2         => 5,
	TRANSACTION_ENDS_1 => 6,
	TRANSACTION_ENDS_2 => 7,
};

# Test concurrent update in table row.
my $node = get_new_node('main');
$node->init;
$node->start;
$node->safe_psql('postgres',
    'CREATE UNLOGGED TABLE xy (x integer, y integer); '
  . 'INSERT INTO xy VALUES (1, 2), (2, 3);');

my $script_serialization = $node->basedir . '/pgbench_script_serialization';
append_to_file($script_serialization,
		"\\set delta random(-5000, 5000)\n"
	  . "BEGIN;\n"
	  . "SELECT pg_sleep(1);\n"
	  . "UPDATE xy SET y = y + :delta "
	  . "WHERE x = 1 AND pg_advisory_lock(0) IS NOT NULL;\n"
	  . "SELECT pg_advisory_unlock_all();\n"
	  . "END;\n");

my $script_deadlocks1 = $node->basedir . '/pgbench_script_deadlocks1';
append_to_file($script_deadlocks1,
		"BEGIN;\n"
	  . "SELECT pg_advisory_lock(" . DEADLOCK_1 . ");\n"
	  . "SELECT pg_advisory_lock(" . WAIT_PGBENCH_2 . ");\n"
	  . "SELECT pg_advisory_lock(" . DEADLOCK_2 . ");\n"
	  . "END;\n"
	  . "SELECT pg_advisory_unlock_all();\n"
	  . "SELECT pg_advisory_lock(" . TRANSACTION_ENDS_1 . ");\n"
	  . "SELECT pg_advisory_unlock_all();");

my $script_deadlocks2 = $node->basedir . '/pgbench_script_deadlocks2';
append_to_file($script_deadlocks2,
		"BEGIN;\n"
	  . "SELECT pg_advisory_lock(" . DEADLOCK_2 . ");\n"
	  . "SELECT pg_advisory_lock(" . DEADLOCK_1 . ");\n"
	  . "END;\n"
	  . "SELECT pg_advisory_unlock_all();\n"
	  . "SELECT pg_advisory_lock(" . TRANSACTION_ENDS_2 . ");\n"
	  . "SELECT pg_advisory_unlock_all();");

sub test_pgbench_serialization_errors
{
	my ($max_tries, $latency_limit, $test_name) = @_;

	my $isolation_level = REPEATABLE_READ;
	my $isolation_level_shell = $isolation_level_shell[$isolation_level];

	local $ENV{PGPORT} = $node->port;
	local $ENV{PGOPTIONS} =
		"-c default_transaction_isolation=" . $isolation_level_shell;
	print "# PGOPTIONS: " . $ENV{PGOPTIONS} . "\n";

	my ($h_psql, $in_psql, $out_psql);
	my ($h_pgbench, $in_pgbench, $out_pgbench, $err_pgbench);

	# Open a psql session, run a parallel transaction and aquire an advisory
	# lock:
	print "# Starting psql\n";
	$h_psql = IPC::Run::start [ 'psql' ], \$in_psql, \$out_psql;

	$in_psql = "begin;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /BEGIN/;

	$in_psql =
		"update xy set y = y + 1 "
	  . "where x = 1 and pg_advisory_lock(0) is not null;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /UPDATE 1/;

	my $retry_options =
		($max_tries ? "--max-tries $max_tries" : "")
	  . ($latency_limit ? "--latency-limit $latency_limit" : "");

	# Start pgbench:
	my @command = (
		qw(pgbench --no-vacuum --transactions 1 --debug fails --file),
		$script_serialization,
		split /\s+/, $retry_options);
	print "# Running: " . join(" ", @command) . "\n";
	$h_pgbench = IPC::Run::start \@command, \$in_pgbench, \$out_pgbench,
	  \$err_pgbench;

	# Wait until pgbench also tries to acquire the same advisory lock:
	do
	{
		$in_psql =
			"select * from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = 0::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /1 row/);

	# In psql, commit the transaction, release advisory locks and end the
	# session:
	$in_psql = "end;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /COMMIT/;

	$in_psql = "select pg_advisory_unlock_all();\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_unlock_all/;

	$in_psql = "\\q\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() while length $in_psql;

	$h_psql->finish();

	# Get pgbench results
	$h_pgbench->pump() until length $out_pgbench;
	$h_pgbench->finish();

	# On Windows, the exit status of the process is returned directly as the
	# process's exit code, while on Unix, it's returned in the high bits
	# of the exit code (see WEXITSTATUS macro in the standard <sys/wait.h>
	# header file). IPC::Run's result function always returns exit code >> 8,
	# assuming the Unix convention, which will always return 0 on Windows as
	# long as the process was not terminated by an exception. To work around
	# that, use $h->full_result on Windows instead.
	my $result =
	    ($Config{osname} eq "MSWin32")
	  ? ($h_pgbench->full_results)[0]
	  : $h_pgbench->result(0);

	# Check pgbench results
	ok(!$result, "@command exit code 0");

	like($out_pgbench,
		qr{processed: 0/1},
		"$test_name: check processed transactions");

	like($out_pgbench,
		qr{number of errors: 1 \(100\.000%\)},
		"$test_name: check errors");

	like($out_pgbench,
		qr{^((?!number of retried)(.|\n))*$},
		"$test_name: check retried");

	if ($max_tries)
	{
		like($out_pgbench,
			qr{maximum number of tries: $max_tries},
			"$test_name: check the maximum number of tries");
	}
	else
	{
		like($out_pgbench,
			qr{^((?!maximum number of tries)(.|\n))*$},
			"$test_name: check the maximum number of tries");
	}

	if ($latency_limit)
	{
		like($out_pgbench,
			qr{number of transactions above the $latency_limit\.0 ms latency limit: 1/1 \(100.000 \%\) \(including errors\)},
			"$test_name: check transactions above latency limit");
	}
	else
	{
		like($out_pgbench,
			qr{^((?!latency limit)(.|\n))*$},
			"$test_name: check transactions above latency limit");
	}

	my $pattern =
		"client 0 got a failure in command 3 \\(SQL\\) of script 0; "
	  . "ERROR:  could not serialize access due to concurrent update";

	like($err_pgbench,
		qr{$pattern},
		"$test_name: check serialization failure");
}

sub test_pgbench_serialization_failures
{
	my $isolation_level = REPEATABLE_READ;
	my $isolation_level_shell = $isolation_level_shell[$isolation_level];

	local $ENV{PGPORT} = $node->port;
	local $ENV{PGOPTIONS} =
		"-c default_transaction_isolation=" . $isolation_level_shell;
	print "# PGOPTIONS: " . $ENV{PGOPTIONS} . "\n";

	my ($h_psql, $in_psql, $out_psql);
	my ($h_pgbench, $in_pgbench, $out_pgbench, $err_pgbench);

	# Open a psql session, run a parallel transaction and aquire an advisory
	# lock:
	print "# Starting psql\n";
	$h_psql = IPC::Run::start [ 'psql' ], \$in_psql, \$out_psql;

	$in_psql = "begin;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /BEGIN/;

	$in_psql =
		"update xy set y = y + 1 "
	  . "where x = 1 and pg_advisory_lock(0) is not null;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /UPDATE 1/;

	# Start pgbench:
	my @command = (
		qw(pgbench --no-vacuum --transactions 1 --debug all --max-tries 2),
		"--file",
		$script_serialization);
	print "# Running: " . join(" ", @command) . "\n";
	$h_pgbench = IPC::Run::start \@command, \$in_pgbench, \$out_pgbench,
	  \$err_pgbench;

	# Wait until pgbench also tries to acquire the same advisory lock:
	do
	{
		$in_psql =
			"select * from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = 0::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /1 row/);

	# In psql, commit the transaction, release advisory locks and end the
	# session:
	$in_psql = "end;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /COMMIT/;

	$in_psql = "select pg_advisory_unlock_all();\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_unlock_all/;

	$in_psql = "\\q\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() while length $in_psql;

	$h_psql->finish();

	# Get pgbench results
	$h_pgbench->pump() until length $out_pgbench;
	$h_pgbench->finish();

	# On Windows, the exit status of the process is returned directly as the
	# process's exit code, while on Unix, it's returned in the high bits
	# of the exit code (see WEXITSTATUS macro in the standard <sys/wait.h>
	# header file). IPC::Run's result function always returns exit code >> 8,
	# assuming the Unix convention, which will always return 0 on Windows as
	# long as the process was not terminated by an exception. To work around
	# that, use $h->full_result on Windows instead.
	my $result =
	    ($Config{osname} eq "MSWin32")
	  ? ($h_pgbench->full_results)[0]
	  : $h_pgbench->result(0);

	# Check pgbench results
	ok(!$result, "@command exit code 0");

	like($out_pgbench,
		qr{processed: 1/1},
		"concurrent update with retrying: check processed transactions");

	like($out_pgbench,
		qr{^((?!number of errors)(.|\n))*$},
		"concurrent update with retrying: check errors");

	like($out_pgbench,
		qr{number of retried: 1 \(100\.000%\)},
		"concurrent update with retrying: check retried");

	like($out_pgbench,
		qr{number of retries: 1},
		"concurrent update with retrying: check retries");

	like($out_pgbench,
		qr{latency average = \d+\.\d{3} ms\n},
		"concurrent update with retrying: check latency average");

	my $pattern =
		"client 0 sending UPDATE xy SET y = y \\+ (-?\\d+) "
	  . "WHERE x = 1 AND pg_advisory_lock\\(0\\) IS NOT NULL;\n"
	  . "(client 0 receiving\n)+"
	  . "client 0 got a failure in command 3 \\(SQL\\) of script 0; "
	  . "ERROR:  could not serialize access due to concurrent update\n\n"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g2+"
	  . "client 0 continues a failed transaction in command 4 \\(SQL\\) of script 0; "
	  . "ERROR:  current transaction is aborted, commands ignored until end of transaction block\n\n"
	  . "client 0 sending END;\n"
	  . "\\g2+"
	  . "client 0 repeats the failed transaction \\(try 2/2\\)\n"
	  . "client 0 executing \\\\set delta\n"
	  . "client 0 sending BEGIN;\n"
	  . "\\g2+"
	  . "client 0 sending SELECT pg_sleep\\(1\\);\n"
	  . "\\g2+"
	  . "client 0 sending UPDATE xy SET y = y \\+ \\g1 "
	  . "WHERE x = 1 AND pg_advisory_lock\\(0\\) IS NOT NULL;";

	like($err_pgbench,
		qr{$pattern},
		"concurrent update with retrying: check the retried transaction");
}

sub test_pgbench_deadlock_errors
{
	my $isolation_level = READ_COMMITTED;
	my $isolation_level_shell = $isolation_level_shell[$isolation_level];

	local $ENV{PGPORT} = $node->port;
	local $ENV{PGOPTIONS} =
		"-c default_transaction_isolation=" . $isolation_level_shell;
	print "# PGOPTIONS: " . $ENV{PGOPTIONS} . "\n";

	my ($h_psql, $in_psql, $out_psql);
	my ($h1, $in1, $out1, $err1);
	my ($h2, $in2, $out2, $err2);

	# Open a psql session and aquire an advisory lock:
	print "# Starting psql\n";
	$h_psql = IPC::Run::start [ 'psql' ], \$in_psql, \$out_psql;

	$in_psql =
		"select pg_advisory_lock(" . WAIT_PGBENCH_2 . ") "
	  . "as pg_advisory_lock_" . WAIT_PGBENCH_2 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_lock_@{[ WAIT_PGBENCH_2 ]}/;

	# Run the first pgbench:
	my @command1 = (
		qw(pgbench --no-vacuum --transactions 1 --debug fails --file),
		$script_deadlocks1);
	print "# Running: " . join(" ", @command1) . "\n";
	$h1 = IPC::Run::start \@command1, \$in1, \$out1, \$err1;

	# Wait until the first pgbench also tries to acquire the same advisory lock:
	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . WAIT_PGBENCH_2 . "_zero' "
		  . "else '" . WAIT_PGBENCH_2 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . WAIT_PGBENCH_2
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ WAIT_PGBENCH_2 ]}_not_zero/);

	# Run the second pgbench:
	my @command2 = (
		qw(pgbench --no-vacuum --transactions 1 --debug fails --file),
		$script_deadlocks2);
	print "# Running: " . join(" ", @command2) . "\n";
	$h2 = IPC::Run::start \@command2, \$in2, \$out2, \$err2;

	# Wait until the second pgbench tries to acquire the lock held by the first
	# pgbench:
	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . DEADLOCK_1 . "_zero' "
		  . "else '" . DEADLOCK_1 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . DEADLOCK_1
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ DEADLOCK_1 ]}_not_zero/);

	# In the psql session, release the lock that the first pgbench is waiting
	# for and end the session:
	$in_psql =
		"select pg_advisory_unlock(" . WAIT_PGBENCH_2 . ") "
	  . "as pg_advisory_unlock_" . WAIT_PGBENCH_2 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_unlock_@{[ WAIT_PGBENCH_2 ]}/;

	$in_psql = "\\q\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() while length $in_psql;

	$h_psql->finish();

	# Get results from all pgbenches:
	$h1->pump() until length $out1;
	$h1->finish();

	$h2->pump() until length $out2;
	$h2->finish();

	# On Windows, the exit status of the process is returned directly as the
	# process's exit code, while on Unix, it's returned in the high bits
	# of the exit code (see WEXITSTATUS macro in the standard <sys/wait.h>
	# header file). IPC::Run's result function always returns exit code >> 8,
	# assuming the Unix convention, which will always return 0 on Windows as
	# long as the process was not terminated by an exception. To work around
	# that, use $h->full_result on Windows instead.
	my $result1 =
	    ($Config{osname} eq "MSWin32")
	  ? ($h1->full_results)[0]
	  : $h1->result(0);

	my $result2 =
	    ($Config{osname} eq "MSWin32")
	  ? ($h2->full_results)[0]
	  : $h2->result(0);

	# Check all pgbench results
	ok(!$result1, "@command1 exit code 0");
	ok(!$result2, "@command2 exit code 0");

	# The first or second pgbench should get a deadlock error
	ok((($out1 =~ /processed: 0\/1/ and $out2 =~ /processed: 1\/1/) or
		($out2 =~ /processed: 0\/1/ and $out1 =~ /processed: 1\/1/)),
		"concurrent deadlock update: check processed transactions");

	ok((($out1 =~ /number of errors: 1 \(100\.000%\)/ and
		 $out2 =~ /^((?!number of errors)(.|\n))*$/) or
		($out2 =~ /number of errors: 1 \(100\.000%\)/ and
		 $out1 =~ /^((?!number of errors)(.|\n))*$/)),
		"concurrent deadlock update: check errors");

	ok(($err1 =~ /client 0 got a failure in command 3 \(SQL\) of script 0; ERROR:  deadlock detected/ or
		$err2 =~ /client 0 got a failure in command 2 \(SQL\) of script 0; ERROR:  deadlock detected/),
		"concurrent deadlock update: check deadlock failure");

	# Both pgbenches do not have retried transactions
	like($out1 . $out2,
		qr{^((?!number of retried)(.|\n))*$},
		"concurrent deadlock update: check retried");
}

sub test_pgbench_deadlock_failures
{
	my $isolation_level = READ_COMMITTED;
	my $isolation_level_shell = $isolation_level_shell[$isolation_level];

	local $ENV{PGPORT} = $node->port;
	local $ENV{PGOPTIONS} =
		"-c default_transaction_isolation=" . $isolation_level_shell;
	print "# PGOPTIONS: " . $ENV{PGOPTIONS} . "\n";

	my ($h_psql, $in_psql, $out_psql);
	my ($h1, $in1, $out1, $err1);
	my ($h2, $in2, $out2, $err2);

	# Open a psql session and aquire an advisory lock:
	print "# Starting psql\n";
	$h_psql = IPC::Run::start [ 'psql' ], \$in_psql, \$out_psql;

	$in_psql =
		"select pg_advisory_lock(" . WAIT_PGBENCH_2 . ") "
	  . "as pg_advisory_lock_" . WAIT_PGBENCH_2 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_lock_@{[ WAIT_PGBENCH_2 ]}/;

	# Run the first pgbench:
	my @command1 = (
		qw(pgbench --no-vacuum --transactions 1 --debug all --max-tries 2),
		"--file",
		$script_deadlocks1);
	print "# Running: " . join(" ", @command1) . "\n";
	$h1 = IPC::Run::start \@command1, \$in1, \$out1, \$err1;

	# Wait until the first pgbench also tries to acquire the same advisory lock:
	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . WAIT_PGBENCH_2 . "_zero' "
		  . "else '" . WAIT_PGBENCH_2 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . WAIT_PGBENCH_2
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ WAIT_PGBENCH_2 ]}_not_zero/);

	# Run the second pgbench:
	my @command2 = (
		qw(pgbench --no-vacuum --transactions 1 --debug all --max-tries 2),
		"--file",
		$script_deadlocks2);
	print "# Running: " . join(" ", @command2) . "\n";
	$h2 = IPC::Run::start \@command2, \$in2, \$out2, \$err2;

	# Wait until the second pgbench tries to acquire the lock held by the first
	# pgbench:
	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . DEADLOCK_1 . "_zero' "
		  . "else '" . DEADLOCK_1 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . DEADLOCK_1
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ DEADLOCK_1 ]}_not_zero/);

	# In the psql session, acquire the locks that pgbenches will wait for:
	$in_psql =
		"select pg_advisory_lock(" . TRANSACTION_ENDS_1 . ") "
	  . "as pg_advisory_lock_" . TRANSACTION_ENDS_1 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_lock_@{[ TRANSACTION_ENDS_1 ]}/;

	$in_psql =
		"select pg_advisory_lock(" . TRANSACTION_ENDS_2 . ") "
	  . "as pg_advisory_lock_" . TRANSACTION_ENDS_2 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_lock_@{[ TRANSACTION_ENDS_2 ]}/;

	# In the psql session, release the lock that the first pgbench is waiting
	# for:
	$in_psql =
		"select pg_advisory_unlock(" . WAIT_PGBENCH_2 . ") "
	  . "as pg_advisory_unlock_" . WAIT_PGBENCH_2 . ";\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_unlock_@{[ WAIT_PGBENCH_2 ]}/;

	# Wait until pgbenches try to acquire the locks held by the psql session:
	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . TRANSACTION_ENDS_1 . "_zero' "
		  . "else '" . TRANSACTION_ENDS_1 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . TRANSACTION_ENDS_1
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ TRANSACTION_ENDS_1 ]}_not_zero/);

	do
	{
		$in_psql =
			"select case count(*) "
		  . "when 0 then '" . TRANSACTION_ENDS_2 . "_zero' "
		  . "else '" . TRANSACTION_ENDS_2 . "_not_zero' end "
		  . "from pg_locks where "
		  . "locktype = 'advisory' and "
		  . "objsubid = 1 and "
		  . "((classid::bigint << 32) | objid::bigint = "
		  . TRANSACTION_ENDS_2
		  . "::bigint) and "
		  . "not granted;\n";
		print "# Running in psql: " . join(" ", $in_psql);
		$h_psql->pump() while length $in_psql;
	} while ($out_psql !~ /@{[ TRANSACTION_ENDS_2 ]}_not_zero/);

	# In the psql session, release advisory locks and end the session:
	$in_psql = "select pg_advisory_unlock_all() as pg_advisory_unlock_all;\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() until $out_psql =~ /pg_advisory_unlock_all/;

	$in_psql = "\\q\n";
	print "# Running in psql: " . join(" ", $in_psql);
	$h_psql->pump() while length $in_psql;

	$h_psql->finish();

	# Get results from all pgbenches:
	$h1->pump() until length $out1;
	$h1->finish();

	$h2->pump() until length $out2;
	$h2->finish();

	# On Windows, the exit status of the process is returned directly as the
	# process's exit code, while on Unix, it's returned in the high bits
	# of the exit code (see WEXITSTATUS macro in the standard <sys/wait.h>
	# header file). IPC::Run's result function always returns exit code >> 8,
	# assuming the Unix convention, which will always return 0 on Windows as
	# long as the process was not terminated by an exception. To work around
	# that, use $h->full_result on Windows instead.
	my $result1 =
	    ($Config{osname} eq "MSWin32")
	  ? ($h1->full_results)[0]
	  : $h1->result(0);

	my $result2 =
	    ($Config{osname} eq "MSWin32")
	  ? ($h2->full_results)[0]
	  : $h2->result(0);

	# Check all pgbench results
	ok(!$result1, "@command1 exit code 0");
	ok(!$result2, "@command2 exit code 0");

	like($out1,
		qr{processed: 1/1},
		"concurrent deadlock update with retrying: pgbench 1: "
	  . "check processed transactions");
	like($out2,
		qr{processed: 1/1},
		"concurrent deadlock update with retrying: pgbench 2: "
	  . "check processed transactions");

	# The first or second pgbench should get a deadlock error which was retried:
	like($out1 . $out2,
		qr{^((?!number of errors)(.|\n))*$},
		"concurrent deadlock update with retrying: check errors");

	ok((($out1 =~ /number of retried: 1 \(100\.000%\)/ and
		 $out2 =~ /^((?!number of retried)(.|\n))*$/) or
		($out2 =~ /number of retried: 1 \(100\.000%\)/ and
		 $out1 =~ /^((?!number of retried)(.|\n))*$/)),
		"concurrent deadlock update with retrying: check retries");

	my $pattern1 =
		"client 0 sending BEGIN;\n"
	  . "(client 0 receiving\n)+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . WAIT_PGBENCH_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 got a failure in command 3 \\(SQL\\) of script 0; "
	  . "ERROR:  deadlock detected\n"
	  . "((?!client 0)(.|\n))*"
	  . "client 0 sending END;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . TRANSACTION_ENDS_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 repeats the failed transaction \\(try 2/2\\)\n"
	  . "client 0 sending BEGIN;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . WAIT_PGBENCH_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending END;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . TRANSACTION_ENDS_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+";

	my $pattern2 =
		"client 0 sending BEGIN;\n"
	  . "(client 0 receiving\n)+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 got a failure in command 2 \\(SQL\\) of script 0; "
	  . "ERROR:  deadlock detected\n"
	  . "((?!client 0)(.|\n))*"
	  . "client 0 sending END;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . TRANSACTION_ENDS_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 repeats the failed transaction \\(try 2/2\\)\n"
	  . "client 0 sending BEGIN;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . DEADLOCK_1 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending END;\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_lock\\(" . TRANSACTION_ENDS_2 . "\\);\n"
	  . "\\g1+"
	  . "client 0 sending SELECT pg_advisory_unlock_all\\(\\);\n"
	  . "\\g1+";

	ok(($err1 =~ /$pattern1/ or $err2 =~ /$pattern2/),
		"concurrent deadlock update with retrying: "
	  . "check the retried transaction");
}

test_pgbench_serialization_errors(
								1,      # --max-tries
								0,      # --latency-limit (will not be used)
								"concurrent update");
test_pgbench_serialization_errors(
								0,	    # --max-tries (will not be used)
								900,    # --latency-limit
								"concurrent update with maximum time of tries");

test_pgbench_serialization_failures();

test_pgbench_deadlock_errors();
test_pgbench_deadlock_failures();

#done
$node->stop;
