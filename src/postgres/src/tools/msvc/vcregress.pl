# -*-perl-*- hey - emacs - this is a perl file

# Copyright (c) 2021-2022, PostgreSQL Global Development Group

# src/tools/msvc/vcregress.pl

use strict;
use warnings;

our $config;

use Cwd;
use File::Basename;
use File::Copy;
use File::Find ();
use File::Path qw(rmtree);
use File::Spec qw(devnull);

use FindBin;
use lib $FindBin::RealBin;

use Install qw(Install);

my $startdir = getcwd();

chdir "../../.." if (-d "../../../src/tools/msvc");

my $topdir         = getcwd();
my $tmp_installdir = "$topdir/tmp_install";

do './src/tools/msvc/config_default.pl';
do './src/tools/msvc/config.pl' if (-f 'src/tools/msvc/config.pl');

my $devnull = File::Spec->devnull;

# These values are defaults that can be overridden by the calling environment
# (see buildenv.pl processing below).  We assume that the ones listed here
# always exist by default.  Other values may optionally be set for bincheck
# or taptest, see set_command_env() below.
# c.f. src/Makefile.global.in and configure.ac
$ENV{TAR} ||= 'tar';

# buildenv.pl is for specifying the build environment settings
# it should contain lines like:
# $ENV{PATH} = "c:/path/to/bison/bin;$ENV{PATH}";

if (-e "src/tools/msvc/buildenv.pl")
{
	do "./src/tools/msvc/buildenv.pl";
}

my $what = shift || "";
if ($what =~
	/^(check|installcheck|plcheck|contribcheck|modulescheck|ecpgcheck|isolationcheck|upgradecheck|bincheck|recoverycheck|taptest)$/i
  )
{
	$what = uc $what;
}
else
{
	usage();
}

# use a capital C here because config.pl has $config
my $Config = -e "release/postgres/postgres.exe" ? "Release" : "Debug";

copy("$Config/refint/refint.dll",                 "src/test/regress");
copy("$Config/autoinc/autoinc.dll",               "src/test/regress");
copy("$Config/regress/regress.dll",               "src/test/regress");
copy("$Config/dummy_seclabel/dummy_seclabel.dll", "src/test/regress");

# Configuration settings used by TAP tests
$ENV{with_ssl}    = $config->{openssl} ? 'openssl' : 'no';
$ENV{with_ldap}   = $config->{ldap}    ? 'yes'     : 'no';
$ENV{with_icu}    = $config->{icu}     ? 'yes'     : 'no';
$ENV{with_gssapi} = $config->{gss}     ? 'yes'     : 'no';
$ENV{with_krb_srvnam} = $config->{krb_srvnam} || 'postgres';
$ENV{with_readline} = 'no';

$ENV{PATH} = "$topdir/$Config/libpq;$ENV{PATH}";

if ($ENV{PERL5LIB})
{
	$ENV{PERL5LIB} = "$topdir/src/tools/msvc;$ENV{PERL5LIB}";
}
else
{
	$ENV{PERL5LIB} = "$topdir/src/tools/msvc";
}

my $maxconn = "";
$maxconn = "--max-connections=$ENV{MAX_CONNECTIONS}"
  if $ENV{MAX_CONNECTIONS};

my $temp_config = "";
$temp_config = "--temp-config=\"$ENV{TEMP_CONFIG}\""
  if $ENV{TEMP_CONFIG};

chdir "src/test/regress";

my %command = (
	CHECK          => \&check,
	PLCHECK        => \&plcheck,
	INSTALLCHECK   => \&installcheck,
	ECPGCHECK      => \&ecpgcheck,
	CONTRIBCHECK   => \&contribcheck,
	MODULESCHECK   => \&modulescheck,
	ISOLATIONCHECK => \&isolationcheck,
	BINCHECK       => \&bincheck,
	RECOVERYCHECK  => \&recoverycheck,
	UPGRADECHECK   => \&upgradecheck,     # no-op
	TAPTEST        => \&taptest,);

my $proc = $command{$what};

exit 3 unless $proc;

&$proc(@ARGV);

exit 0;

########################################################################

# Helper function for set_command_env, to set one environment command.
sub set_single_env
{
	my $envname    = shift;
	my $envdefault = shift;

	# If a command is defined by the environment, just use it.
	return if (defined($ENV{$envname}));

	# Nothing is defined, so attempt to assign a default.  The command
	# may not be in the current environment, hence check if it can be
	# executed.
	my $rc = system("$envdefault --version >$devnull 2>&1");

	# Set the environment to the default if it exists, else leave it.
	$ENV{$envname} = $envdefault if $rc == 0;
	return;
}

# Set environment values for various command types.  These can be used
# in the TAP tests.
sub set_command_env
{
	set_single_env('GZIP_PROGRAM', 'gzip');
	set_single_env('LZ4',          'lz4');
	set_single_env('ZSTD',         'zstd');
}

sub installcheck_internal
{
	my ($schedule, @EXTRA_REGRESS_OPTS) = @_;
	# for backwards compatibility, "serial" runs the tests in
	# parallel_schedule one by one.
	my $maxconn = $maxconn;
	$maxconn  = "--max-connections=1" if $schedule eq 'serial';
	$schedule = 'parallel'            if $schedule eq 'serial';

	my @args = (
		"../../../$Config/pg_regress/pg_regress",
		"--dlpath=.",
		"--bindir=../../../$Config/psql",
		"--schedule=${schedule}_schedule",
		"--max-concurrent-tests=20",
		"--encoding=SQL_ASCII",
		"--no-locale");
	push(@args, $maxconn) if $maxconn;
	push(@args, @EXTRA_REGRESS_OPTS);
	system(@args);
	my $status = $? >> 8;
	exit $status if $status;
	return;
}

sub installcheck
{
	my $schedule = shift || 'serial';
	installcheck_internal($schedule);
	return;
}

sub check
{
	my $schedule = shift || 'parallel';
	# for backwards compatibility, "serial" runs the tests in
	# parallel_schedule one by one.
	my $maxconn = $maxconn;
	$maxconn  = "--max-connections=1" if $schedule eq 'serial';
	$schedule = 'parallel'            if $schedule eq 'serial';

	InstallTemp();
	chdir "${topdir}/src/test/regress";
	my @args = (
		"../../../$Config/pg_regress/pg_regress",
		"--dlpath=.",
		"--bindir=",
		"--schedule=${schedule}_schedule",
		"--max-concurrent-tests=20",
		"--encoding=SQL_ASCII",
		"--no-locale",
		"--temp-instance=./tmp_check");
	push(@args, $maxconn)     if $maxconn;
	push(@args, $temp_config) if $temp_config;
	system(@args);
	my $status = $? >> 8;
	exit $status if $status;
	return;
}

sub ecpgcheck
{
	my $msbflags = $ENV{MSBFLAGS} || "";
	chdir $startdir;
	system("msbuild ecpg_regression.proj $msbflags /p:config=$Config");
	my $status = $? >> 8;
	exit $status if $status;
	InstallTemp();
	chdir "$topdir/src/interfaces/ecpg/test";
	my $schedule = "ecpg";
	my @args     = (
		"../../../../$Config/pg_regress_ecpg/pg_regress_ecpg",
		"--bindir=",
		"--dbname=ecpg1_regression,ecpg2_regression",
		"--create-role=regress_ecpg_user1,regress_ecpg_user2",
		"--schedule=${schedule}_schedule",
		"--encoding=SQL_ASCII",
		"--no-locale",
		"--temp-instance=./tmp_chk");
	push(@args, $maxconn) if $maxconn;
	system(@args);
	$status = $? >> 8;
	exit $status if $status;
	return;
}

sub isolationcheck
{
	chdir "../isolation";
	copy("../../../$Config/isolationtester/isolationtester.exe",
		"../../../$Config/pg_isolation_regress");
	my @args = (
		"../../../$Config/pg_isolation_regress/pg_isolation_regress",
		"--bindir=../../../$Config/psql",
		"--inputdir=.",
		"--schedule=./isolation_schedule");
	push(@args, $maxconn) if $maxconn;
	system(@args);
	my $status = $? >> 8;
	exit $status if $status;
	return;
}

sub tap_check
{
	die "Tap tests not enabled in configuration"
	  unless $config->{tap_tests};

	my @flags;
	foreach my $arg (0 .. scalar(@_) - 1)
	{
		next unless $_[$arg] =~ /^PROVE_FLAGS=(.*)/;
		@flags = split(/\s+/, $1);
		splice(@_, $arg, 1);
		last;
	}

	my $dir = shift;
	chdir $dir;

	# Fetch and adjust PROVE_TESTS, applying glob() to each element
	# defined to build a list of all the tests matching patterns.
	my $prove_tests_val = $ENV{PROVE_TESTS} || "t/*.pl";
	my @prove_tests_array = split(/\s+/, $prove_tests_val);
	my @prove_tests = ();
	foreach (@prove_tests_array)
	{
		push(@prove_tests, glob($_));
	}

	# Fetch and adjust PROVE_FLAGS, handling multiple arguments.
	my $prove_flags_val = $ENV{PROVE_FLAGS} || "";
	my @prove_flags = split(/\s+/, $prove_flags_val);

	my @args = ("prove", @flags, @prove_tests, @prove_flags);

	# adjust the environment for just this test
	local %ENV = %ENV;
	$ENV{PERL5LIB}      = "$topdir/src/test/perl;$ENV{PERL5LIB}";
	$ENV{PG_REGRESS}    = "$topdir/$Config/pg_regress/pg_regress";
	$ENV{REGRESS_SHLIB} = "$topdir/src/test/regress/regress.dll";

	$ENV{TESTDIR} = "$dir";
	my $module = basename $dir;
	# add the module build dir as the second element in the PATH
	$ENV{PATH} =~ s!;!;$topdir/$Config/$module;!;

	rmtree('tmp_check');
	system(@args);
	my $status = $? >> 8;
	return $status;
}

sub bincheck
{
	InstallTemp();

	set_command_env();

	my $mstat = 0;

	# Find out all the existing TAP tests by looking for t/ directories
	# in the tree.
	my @bin_dirs = glob("$topdir/src/bin/*");

	# Process each test
	foreach my $dir (@bin_dirs)
	{
		next unless -d "$dir/t";

		my $status = tap_check($dir);
		$mstat ||= $status;
	}
	exit $mstat if $mstat;
	return;
}

sub taptest
{
	my $dir = shift;
	my @args;

	if ($dir =~ /^PROVE_FLAGS=/)
	{
		push(@args, $dir);
		$dir = shift;
	}

	die "no tests found!" unless -d "$topdir/$dir/t";

	push(@args, "$topdir/$dir");

	InstallTemp();

	set_command_env();

	my $status = tap_check(@args);
	exit $status if $status;
	return;
}

sub plcheck
{
	chdir "$topdir/src/pl";

	foreach my $dir (glob("*/src *"))
	{
		next unless -d "$dir/sql" && -d "$dir/expected";
		my $lang;
		if ($dir eq 'plpgsql/src')
		{
			$lang = 'plpgsql';
		}
		elsif ($dir eq 'tcl')
		{
			$lang = 'pltcl';
		}
		else
		{
			$lang = $dir;
		}
		if ($lang eq 'plpython')
		{
			next
			  unless -d "$topdir/$Config/plpython3";
			$lang = 'plpythonu';
		}
		else
		{
			next unless -d "$topdir/$Config/$lang";
		}
		my @lang_args = ("--load-extension=$lang");
		chdir $dir;
		my @tests = fetchTests();
		if ($lang eq 'plperl')
		{

			# plperl tests will install the extensions themselves
			@lang_args = ();

			# assume we're using this perl to built postgres
			# test if we can run two interpreters in one backend, and if so
			# run the trusted/untrusted interaction tests
			use Config;
			if ($Config{usemultiplicity} eq 'define')
			{
				push(@tests, 'plperl_plperlu');
			}
		}
		elsif ($lang eq 'plpythonu' && -d "$topdir/$Config/plpython3")
		{
			@lang_args = ();
		}

		# Move on if no tests are listed.
		next if (scalar @tests == 0);

		print
		  "============================================================\n";
		print "Checking $lang\n";
		my @args = (
			"$topdir/$Config/pg_regress/pg_regress",
			"--bindir=$topdir/$Config/psql",
			"--dbname=pl_regression", @lang_args, @tests);
		system(@args);
		my $status = $? >> 8;
		exit $status if $status;
		chdir "$topdir/src/pl";
	}

	chdir "$topdir";
	return;
}

sub subdircheck
{
	my $module = shift;

	if (   !-d "$module/sql"
		|| !-d "$module/expected"
		|| (!-f "$module/GNUmakefile" && !-f "$module/Makefile"))
	{
		return;
	}

	chdir $module;
	my @tests = fetchTests();

	# Leave if no tests are listed in the module.
	if (scalar @tests == 0)
	{
		chdir "..";
		return;
	}

	my @opts = fetchRegressOpts();

	print "============================================================\n";
	print "Checking $module\n";
	my @args = (
		"$topdir/$Config/pg_regress/pg_regress",
		"--bindir=${topdir}/${Config}/psql",
		"--dbname=contrib_regression", @opts, @tests);
	print join(' ', @args), "\n";
	system(@args);
	chdir "..";
	return;
}

sub contribcheck
{
	chdir "../../../contrib";
	my $mstat = 0;
	foreach my $module (glob("*"))
	{
		# these configuration-based exclusions must match Install.pm
		next if ($module eq "uuid-ossp"  && !defined($config->{uuid}));
		next if ($module eq "sslinfo"    && !defined($config->{openssl}));
		next if ($module eq "pgcrypto"   && !defined($config->{openssl}));
		next if ($module eq "xml2"       && !defined($config->{xml}));
		next if ($module =~ /_plperl$/   && !defined($config->{perl}));
		next if ($module =~ /_plpython$/ && !defined($config->{python}));
		next if ($module eq "sepgsql");

		subdircheck($module);
		my $status = $? >> 8;
		$mstat ||= $status;
	}
	exit $mstat if $mstat;
	return;
}

sub modulescheck
{
	chdir "../../../src/test/modules";
	my $mstat = 0;
	foreach my $module (glob("*"))
	{
		subdircheck($module);
		my $status = $? >> 8;
		$mstat ||= $status;
	}
	exit $mstat if $mstat;
	return;
}

sub recoverycheck
{
	InstallTemp();

	my $dir    = "$topdir/src/test/recovery";
	my $status = tap_check($dir);
	exit $status if $status;
	return;
}

# Run "initdb", then reconfigure authentication.
sub standard_initdb
{
	return (
		system('initdb', '-N') == 0 and system(
			"$topdir/$Config/pg_regress/pg_regress", '--config-auth',
			$ENV{PGDATA}) == 0);
}

# This is similar to appendShellString().  Perl system(@args) bypasses
# cmd.exe, so omit the caret escape layer.
sub quote_system_arg
{
	my $arg = shift;

	# Change N >= 0 backslashes before a double quote to 2N+1 backslashes.
	$arg =~ s/(\\*)"/${\($1 . $1)}\\"/gs;

	# Change N >= 1 backslashes at end of argument to 2N backslashes.
	$arg =~ s/(\\+)$/${\($1 . $1)}/gs;

	# Wrap the whole thing in unescaped double quotes.
	return "\"$arg\"";
}

sub upgradecheck
{
	# pg_upgrade is now handled by bincheck, but keep this target for
	# backward compatibility.
	print "upgradecheck is a no-op, use bincheck instead.\n";
	return;
}

sub fetchRegressOpts
{
	my $handle;
	open($handle, '<', "GNUmakefile")
	  || open($handle, '<', "Makefile")
	  || die "Could not open Makefile";
	local ($/) = undef;
	my $m = <$handle>;
	close($handle);
	my @opts;

	$m =~ s{\\\r?\n}{}g;
	if ($m =~ /^\s*REGRESS_OPTS\s*\+?=(.*)/m)
	{

		# Substitute known Makefile variables, then ignore options that retain
		# an unhandled variable reference.  Ignore anything that isn't an
		# option starting with "--".
		@opts = grep { !/\$\(/ && /^--/ }
		  map { (my $x = $_) =~ s/\Q$(top_builddir)\E/\"$topdir\"/; $x; }
		  split(/\s+/, $1);
	}
	if ($m =~ /^\s*ENCODING\s*=\s*(\S+)/m)
	{
		push @opts, "--encoding=$1";
	}
	if ($m =~ /^\s*NO_LOCALE\s*=\s*\S+/m)
	{
		push @opts, "--no-locale";
	}
	return @opts;
}

# Fetch the list of tests by parsing a module's Makefile.  An empty
# list is returned if the module does not need to run anything.
sub fetchTests
{
	my $handle;
	open($handle, '<', "GNUmakefile")
	  || open($handle, '<', "Makefile")
	  || die "Could not open Makefile";
	local ($/) = undef;
	my $m = <$handle>;
	close($handle);
	my $t = "";

	$m =~ s{\\\r?\n}{}g;

	# A module specifying NO_INSTALLCHECK does not support installcheck,
	# so bypass its run by returning an empty set of tests.
	if ($m =~ /^\s*NO_INSTALLCHECK\s*=\s*\S+/m)
	{
		return ();
	}

	if ($m =~ /^REGRESS\s*=\s*(.*)$/gm)
	{
		$t = $1;
		$t =~ s/\s+/ /g;

		if ($m =~ /contrib\/pgcrypto/)
		{

			# pgcrypto is special since some tests depend on the
			# configuration of the build

			my $pgptests =
			  $config->{zlib}
			  ? GetTests("ZLIB_TST",     $m)
			  : GetTests("ZLIB_OFF_TST", $m);
			$t =~ s/\$\(CF_PGP_TESTS\)/$pgptests/;
		}
	}

	return split(/\s+/, $t);
}

sub GetTests
{
	my $testname = shift;
	my $m        = shift;
	if ($m =~ /^$testname\s*=\s*(.*)$/gm)
	{
		return $1;
	}
	return "";
}

sub InstallTemp
{
	unless ($ENV{NO_TEMP_INSTALL})
	{
		print "Setting up temp install\n\n";
		Install("$tmp_installdir", "all", $config);
	}
	$ENV{PATH} = "$tmp_installdir/bin;$ENV{PATH}";
	return;
}

sub usage
{
	print STDERR
	  "Usage: vcregress.pl <mode> [<arg>]\n\n",
	  "Options for <mode>:\n",
	  "  bincheck       run tests of utilities in src/bin/\n",
	  "  check          deploy instance and run regression tests on it\n",
	  "  contribcheck   run tests of modules in contrib/\n",
	  "  ecpgcheck      run regression tests of ECPG\n",
	  "  installcheck   run regression tests on existing instance\n",
	  "  isolationcheck run isolation tests\n",
	  "  modulescheck   run tests of modules in src/test/modules/\n",
	  "  plcheck        run tests of PL languages\n",
	  "  recoverycheck  run recovery test suite\n",
	  "  taptest        run an arbitrary TAP test set\n",
	  "  upgradecheck   run tests of pg_upgrade (no-op)\n",
	  "\nOptions for <arg>: (used by check and installcheck)\n",
	  "  serial         serial mode\n",
	  "  parallel       parallel mode\n",
	  "\nOption for <arg>: for taptest\n",
	  "  TEST_DIR       (required) directory where tests reside\n";
	exit(1);
}
