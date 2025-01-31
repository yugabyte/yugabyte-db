# Set of tests for pg_upgrade, including cross-version checks.
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use File::Compare;
use File::Find qw(find);
use File::Path qw(rmtree);

use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::AdjustUpgrade;
use Test::More;

# Generate a database with a name made of a range of ASCII characters.
sub generate_db
{
	my ($node, $prefix, $from_char, $to_char, $suffix) = @_;

	my $dbname = $prefix;
	for my $i ($from_char .. $to_char)
	{
		next if $i == 7 || $i == 10 || $i == 13;    # skip BEL, LF, and CR
		$dbname = $dbname . sprintf('%c', $i);
	}

	$dbname .= $suffix;
	$node->command_ok(
		[ 'createdb', $dbname ],
		"created database with ASCII characters from $from_char to $to_char");
}

# Filter the contents of a dump before its use in a content comparison.
# This returns the path to the filtered dump.
sub filter_dump
{
	my ($is_old, $old_version, $dump_file) = @_;
	my $dump_contents = slurp_file($dump_file);

	if ($is_old)
	{
		$dump_contents = adjust_old_dumpfile($old_version, $dump_contents);
	}
	else
	{
		$dump_contents = adjust_new_dumpfile($old_version, $dump_contents);
	}

	my $dump_file_filtered = "${dump_file}_filtered";
	open(my $dh, '>', $dump_file_filtered)
	  || die "opening $dump_file_filtered";
	print $dh $dump_contents;
	close($dh);

	return $dump_file_filtered;
}

# The test of pg_upgrade requires two clusters, an old one and a new one
# that gets upgraded.  Before running the upgrade, a logical dump of the
# old cluster is taken, and a second logical dump of the new one is taken
# after the upgrade.  The upgrade test passes if there are no differences
# (after filtering) in these two dumps.

# Testing upgrades with an older version of PostgreSQL requires setting up
# two environment variables, as of:
# - "olddump", to point to a dump file that will be used to set up the old
#   instance to upgrade from.
# - "oldinstall", to point to the installation path of the old cluster.
if (   (defined($ENV{olddump}) && !defined($ENV{oldinstall}))
	|| (!defined($ENV{olddump}) && defined($ENV{oldinstall})))
{
	# Not all variables are defined, so leave and die if test is
	# done with an older installation.
	die "olddump or oldinstall is undefined";
}

# Paths to the dumps taken during the tests.
my $tempdir    = PostgreSQL::Test::Utils::tempdir;
my $dump1_file = "$tempdir/dump1.sql";
my $dump2_file = "$tempdir/dump2.sql";

# Initialize node to upgrade
my $oldnode =
  PostgreSQL::Test::Cluster->new('old_node',
	install_path => $ENV{oldinstall});

# To increase coverage of non-standard segment size and group access without
# increasing test runtime, run these tests with a custom setting.
# --allow-group-access and --wal-segsize have been added in v11.
my %node_params = ();
$node_params{extra} = [ '--wal-segsize', '1', '--allow-group-access' ]
  if $oldnode->pg_version >= 11;
$oldnode->init(%node_params);
$oldnode->start;

# The default location of the source code is the root of this directory.
my $srcdir = abs_path("../../..");

# Set up the data of the old instance with a dump or pg_regress.
if (defined($ENV{olddump}))
{
	# Use the dump specified.
	my $olddumpfile = $ENV{olddump};
	die "no dump file found!" unless -e $olddumpfile;

	# Load the dump using the "postgres" database as "regression" does
	# not exist yet, and we are done here.
	$oldnode->command_ok([ 'psql', '-X', '-f', $olddumpfile, 'postgres' ],
		'loaded old dump file');
}
else
{
	# Default is to use pg_regress to set up the old instance.

	# Create databases with names covering most ASCII bytes.  The
	# first name exercises backslashes adjacent to double quotes, a
	# Windows special case.
	generate_db($oldnode, 'regression\\"\\', 1,  45,  '\\\\"\\\\\\');
	generate_db($oldnode, 'regression',      46, 90,  '');
	generate_db($oldnode, 'regression',      91, 127, '');

	# Grab any regression options that may be passed down by caller.
	my $extra_opts = $ENV{EXTRA_REGRESS_OPTS} || "";

	# --dlpath is needed to be able to find the location of regress.so
	# and any libraries the regression tests require.
	my $dlpath = dirname($ENV{REGRESS_SHLIB});

	# --outputdir points to the path where to place the output files.
	my $outputdir = $PostgreSQL::Test::Utils::tmp_check;

	# --inputdir points to the path of the input files.
	my $inputdir = "$srcdir/src/test/regress";

	my $rc =
	  system($ENV{PG_REGRESS}
		  . " $extra_opts "
		  . "--dlpath=\"$dlpath\" "
		  . "--bindir= "
		  . "--host="
		  . $oldnode->host . " "
		  . "--port="
		  . $oldnode->port . " "
		  . "--schedule=$srcdir/src/test/regress/parallel_schedule "
		  . "--max-concurrent-tests=20 "
		  . "--inputdir=\"$inputdir\" "
		  . "--outputdir=\"$outputdir\"");
	if ($rc != 0)
	{
		# Dump out the regression diffs file, if there is one
		my $diffs = "$outputdir/regression.diffs";
		if (-e $diffs)
		{
			print "=== dumping $diffs ===\n";
			print slurp_file($diffs);
			print "=== EOF ===\n";
		}
	}
	is($rc, 0, 'regression tests pass');
}

# Initialize a new node for the upgrade.
my $newnode = PostgreSQL::Test::Cluster->new('new_node');
$newnode->init(%node_params);

my $newbindir = $newnode->config_data('--bindir');
my $oldbindir = $oldnode->config_data('--bindir');

# Before dumping, get rid of objects not existing or not supported in later
# versions. This depends on the version of the old server used, and matters
# only if different major versions are used for the dump.
if (defined($ENV{oldinstall}))
{
	# Consult AdjustUpgrade to find out what we need to do.
	my $dbnames =
	  $oldnode->safe_psql('postgres', qq(SELECT datname FROM pg_database));
	my %dbnames;
	do { $dbnames{$_} = 1; }
	  foreach split /\s+/s, $dbnames;
	my $adjust_cmds =
	  adjust_database_contents($oldnode->pg_version, %dbnames);

	foreach my $updb (keys %$adjust_cmds)
	{
		my $upcmds = join(";\n", @{ $adjust_cmds->{$updb} });

		# For simplicity, use the newer version's psql to issue the commands.
		$newnode->command_ok(
			[
				'psql', '-X',
				'-v',   'ON_ERROR_STOP=1',
				'-c',   $upcmds,
				'-d',   $oldnode->connstr($updb),
			],
			"ran version adaptation commands for database $updb");
	}
}

# Take a dump before performing the upgrade as a base comparison. Note
# that we need to use pg_dumpall from the new node here.
my @dump_command = (
	'pg_dumpall', '--no-sync', '-d', $oldnode->connstr('postgres'),
	'-f', $dump1_file);
# --extra-float-digits is needed when upgrading from a version older than 11.
push(@dump_command, '--extra-float-digits', '0')
  if ($oldnode->pg_version < 12);
$newnode->command_ok(\@dump_command, 'dump before running pg_upgrade');

# After dumping, update references to the old source tree's regress.so
# to point to the new tree.
if (defined($ENV{oldinstall}))
{
	# First, fetch all the references to libraries that are not part
	# of the default path $libdir.
	my $output = $oldnode->safe_psql('regression',
		"SELECT DISTINCT probin::text FROM pg_proc WHERE probin NOT LIKE '\$libdir%';"
	);
	chomp($output);
	my @libpaths = split("\n", $output);

	my $dump_data = slurp_file($dump1_file);

	my $newregresssrc = "$srcdir/src/test/regress";
	foreach (@libpaths)
	{
		my $libpath = $_;
		$libpath = dirname($libpath);
		$dump_data =~ s/$libpath/$newregresssrc/g;
	}

	open my $fh, ">", $dump1_file or die "could not open dump file";
	print $fh $dump_data;
	close $fh;

	# This replaces any references to the old tree's regress.so
	# the new tree's regress.so.  Any references that do *not*
	# match $libdir are switched so as this request does not
	# depend on the path of the old source tree.  This is useful
	# when using an old dump.  Do the operation on all the databases
	# that allow connections so as this includes the regression
	# database and anything the user has set up.
	$output = $oldnode->safe_psql('postgres',
		"SELECT datname FROM pg_database WHERE datallowconn;");
	chomp($output);
	my @datnames = split("\n", $output);
	foreach (@datnames)
	{
		my $datname = $_;
		$oldnode->safe_psql(
			$datname, "UPDATE pg_proc SET probin =
		  regexp_replace(probin, '.*/', '$newregresssrc/')
		  WHERE probin NOT LIKE '\$libdir/%'");
	}
}

# In a VPATH build, we'll be started in the source directory, but we want
# to run pg_upgrade in the build directory so that any files generated finish
# in it, like delete_old_cluster.{sh,bat}.
chdir ${PostgreSQL::Test::Utils::tmp_check};

# Upgrade the instance.
$oldnode->stop;

# Cause a failure at the start of pg_upgrade, this should create the logging
# directory pg_upgrade_output.d but leave it around.  Keep --check for an
# early exit.
command_fails(
	[
		'pg_upgrade', '--no-sync',
		'-d',         $oldnode->data_dir,
		'-D',         $newnode->data_dir,
		'-b',         $oldbindir . '/does/not/exist/',
		'-B',         $newbindir,
		'-s',         $newnode->host,
		'-p',         $oldnode->port,
		'-P',         $newnode->port,
		'--check'
	],
	'run of pg_upgrade --check for new instance with incorrect binary path');
ok(-d $newnode->data_dir . "/pg_upgrade_output.d",
	"pg_upgrade_output.d/ not removed after pg_upgrade failure");
rmtree($newnode->data_dir . "/pg_upgrade_output.d");

# --check command works here, cleans up pg_upgrade_output.d.
command_ok(
	[
		'pg_upgrade', '--no-sync',        '-d', $oldnode->data_dir,
		'-D',         $newnode->data_dir, '-b', $oldbindir,
		'-B',         $newbindir,         '-s', $newnode->host,
		'-p',         $oldnode->port,     '-P', $newnode->port,
		'--check'
	],
	'run of pg_upgrade --check for new instance');
ok(!-d $newnode->data_dir . "/pg_upgrade_output.d",
	"pg_upgrade_output.d/ not removed after pg_upgrade --check success");

# Actual run, pg_upgrade_output.d is removed at the end.
command_ok(
	[
		'pg_upgrade', '--no-sync',        '-d', $oldnode->data_dir,
		'-D',         $newnode->data_dir, '-b', $oldbindir,
		'-B',         $newbindir,         '-s', $newnode->host,
		'-p',         $oldnode->port,     '-P', $newnode->port
	],
	'run of pg_upgrade for new instance');
ok( !-d $newnode->data_dir . "/pg_upgrade_output.d",
	"pg_upgrade_output.d/ removed after pg_upgrade success");

$newnode->start;

# Check if there are any logs coming from pg_upgrade, that would only be
# retained on failure.
my $log_path = $newnode->data_dir . "/pg_upgrade_output.d";
if (-d $log_path)
{
	my @log_files;
	find(
		sub {
			push @log_files, $File::Find::name
			  if $File::Find::name =~ m/.*\.log/;
		},
		$newnode->data_dir . "/pg_upgrade_output.d");
	foreach my $log (@log_files)
	{
		note "=== contents of $log ===\n";
		print slurp_file($log);
		print "=== EOF ===\n";
	}
}

# Second dump from the upgraded instance.
@dump_command = (
	'pg_dumpall', '--no-sync', '-d', $newnode->connstr('postgres'),
	'-f', $dump2_file);
# --extra-float-digits is needed when upgrading from a version older than 11.
push(@dump_command, '--extra-float-digits', '0')
  if ($oldnode->pg_version < 12);
$newnode->command_ok(\@dump_command, 'dump after running pg_upgrade');

# No need to apply filters on the dumps if working on the same version
# for the old and new nodes.
my $dump1_filtered = $dump1_file;
my $dump2_filtered = $dump2_file;
if ($oldnode->pg_version != $newnode->pg_version)
{
	$dump1_filtered = filter_dump(1, $oldnode->pg_version, $dump1_file);
	$dump2_filtered = filter_dump(0, $oldnode->pg_version, $dump2_file);
}

# Compare the two dumps, there should be no differences.
my $compare_res = compare($dump1_filtered, $dump2_filtered);
is($compare_res, 0, 'old and new dumps match after pg_upgrade');

# Provide more context if the dumps do not match.
if ($compare_res != 0)
{
	my ($stdout, $stderr) =
	  run_command([ 'diff', '-u', $dump1_filtered, $dump2_filtered ]);
	print "=== diff of $dump1_filtered and $dump2_filtered\n";
	print "=== stdout ===\n";
	print $stdout;
	print "=== stderr ===\n";
	print $stderr;
	print "=== EOF ===\n";
}

done_testing();
