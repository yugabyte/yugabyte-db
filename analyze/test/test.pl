#!/usr/bin/perl
####################################################################################################################################
# test.pl - pgaudit log analyze regression tests
####################################################################################################################################

####################################################################################################################################
# Perl includes
####################################################################################################################################
use strict;
use warnings FATAL => qw(all);
use Carp;

use Cwd qw(abs_path);
use DBI;
use Getopt::Long;
use File::Basename qw(dirname);
use Pod::Usage;
use IPC::Open3 qw(open3);
use IPC::System::Simple qw(capture);
use Term::ReadKey qw(ReadMode ReadKey);

use lib dirname(abs_path($0)) . '/../lib';
use PgAudit::Wait;

####################################################################################################################################
# Usage
####################################################################################################################################

=head1 NAME

test.pl - PostgreSQL Audit Log Analyzer Regression Tests

=head1 SYNOPSIS

test.pl [options]

 Test Options:
   --no-cleanup         don't cleaup after the last test is complete - useful for debugging

 Configuration Options:
   --pgsql-bin          path to the psql executables (defaults to /usr/local/pgsql/bin)
   --test-path          path where tests are executed (defaults to ./test)
   --quiet, -q          suppress non-error output

 General Options:
   --help               display usage and exit
=cut

####################################################################################################################################
# Constants
####################################################################################################################################
use constant
{
    true  => 1,
    false => 0
};

####################################################################################################################################
# Command line parameters
####################################################################################################################################
my $strPgSqlBin = '/usr/local/pgsql/bin';       # Path of PG binaries to use for this test
my $strTestPath = 'test';                       # Path where testing will occur
my $strUser = getpwuid($>);                     # PG user name
my $strHost = '/tmp';                           # PG default host
my $strDatabase = 'postgres';                   # PG database
my $iPort = 6543;                               # Port to run Postgres on
my $bHelp = false;                              # Display help
my $bNotice = false;                            # Display pgaudit log notices to the console
my $bQuiet = false;                             # Supress output except for errors
my $bNoCleanup = false;                         # Cleanup database on exit
my $bWait = false;                              # Wait for user input after each test

GetOptions ('q|quiet' => \$bQuiet,
            'no-cleanup' => \$bNoCleanup,
            'help' => \$bHelp,
            'notice' => \$bNotice,
            'wait' => \$bWait,
            'pgsql-bin=s' => \$strPgSqlBin,
            'test-path=s' => \$strTestPath)
    or pod2usage(2);

# Display version and exit if requested
if ($bHelp)
{
    syswrite(*STDOUT, "PostgreSQL Audit Log Analyzer Regression Tests\n\n");
    pod2usage();

    exit 0;
}

####################################################################################################################################
# Global variables
####################################################################################################################################
my $hDb;                                        # Master connection to Postgres

####################################################################################################################################
# commandExecute
####################################################################################################################################
sub commandExecute
{
    my $strCommand = shift;
    my $bSuppressError = shift;

    # Set default
    $bSuppressError = defined($bSuppressError) ? $bSuppressError : false;

    # Run the command
    my $strResult;

    eval
    {
        $strResult = capture($strCommand);
    };

    if ($@ && !$bSuppressError)
    {
        confess $@;
    }

    return $strResult;
}

####################################################################################################################################
# log
####################################################################################################################################
sub log
{
    my $strMessage = shift;
    my $bError = shift;
    my $bWaitParam = shift;

    if (defined($strMessage))
    {
        # Set default
        $bError = defined($bError) ? $bError : false;

        if (!$bQuiet)
        {
            if ($strMessage =~ /^\n/)
            {
                syswrite(*STDOUT, "\n");
                $strMessage = substr($strMessage, 1);
            }

            $strMessage =~ s/\n/\n          /g;

            syswrite(*STDOUT, "${strMessage}\n");
        }
    }

    if ($bError)
    {
        exit 1;
    }

    if (defined($bWaitParam) && $bWaitParam && $bWait)
    {
        print "\n[press enter]";

        ReadMode(2);
        ReadKey(0);
        ReadMode(0);

        print "\n";
    }
}

####################################################################################################################################
# pgConnect
####################################################################################################################################
sub pgConnect
{
    my $strUserLocal = shift;
    my $strPasswordLocal = shift;
    my $strHostLocal = shift;
    my $strDatabaseLocal = shift;
    my $bRaiseError = shift;

    # Set defaults
    $strUserLocal = defined($strUserLocal) ? $strUserLocal : $strUser;
    $strPasswordLocal = defined($strPasswordLocal) ? $strPasswordLocal : undef;
    $strHostLocal = defined($strHostLocal) ? $strHostLocal : $strHost;
    $strDatabaseLocal = defined($strDatabaseLocal) ? $strDatabaseLocal : $strDatabase;
    $bRaiseError = defined($bRaiseError) ? $bRaiseError : true;

    # Log Connection
    &log("      DB: connect user ${strUserLocal}, database ${strDatabaseLocal}");

    # Disconnect user session
    # pgDisconnect();

    # Connect to the db
    my $hDbLocal = DBI->connect("dbi:Pg:dbname=${strDatabaseLocal};port=${iPort};host=${strHostLocal}",
                                $strUserLocal, $strPasswordLocal,
                                {AutoCommit => true, RaiseError => false, PrintError => false});

    if (!$hDbLocal && $bRaiseError)
    {
        &log('          unable to connect: ' . $DBI::errstr, true);
    }

    return $hDbLocal;
}

####################################################################################################################################
# pgDisconnect
####################################################################################################################################
sub pgDisconnect
{
    my $hDbLocal = shift;

    # Connect to the db (whether it is local or remote)
    if (defined($hDbLocal ? $hDbLocal : $hDb))
    {
        ($hDbLocal ? $hDbLocal : $hDb)->disconnect;
    }
}

####################################################################################################################################
# pgQueryRow
####################################################################################################################################
sub pgQueryRow
{
    my $strSql = shift;
    my $hDbLocal = shift;
    my $bLog = shift;

    # Log the statement
    if (!defined($bLog) || $bLog)
    {
        &log("  SQL: ${strSql}");
    }

    # Execute the statement
    my $hStatement = ($hDbLocal ? $hDbLocal : $hDb)->prepare($strSql);

    $hStatement->execute();
    my @stryResult = $hStatement->fetchrow_array();

    $hStatement->finish();

    return @stryResult;
}

####################################################################################################################################
# pgQueryTest
####################################################################################################################################
sub pgQueryTest
{
    my $strSql = shift;
    my $hDbLocal = shift;

    # Log the statement
    &log("SQL TEST: ${strSql}");

    my $oWait = waitInit(5);
    my $strError;

    do
    {
        undef($strError);
        my @stryResult = pgQueryRow($strSql, $hDbLocal, false);

        if (@stryResult > 0)
        {
            for (my $iIndex = 0; $iIndex < @stryResult; $iIndex++)
            {
                if (!$stryResult[$iIndex])
                {
                    $strError = 'column ' . ($iIndex + 1) . ' is not true';
                    last;
                }
            }

            if (!defined($strError))
            {
                return;
            }
        }
    }
    while (waitMore($oWait));

    confess 'test failed: ' . (defined($strError) ? $strError : 'test query must return at least one boolean column');
}

####################################################################################################################################
# pgExecute
####################################################################################################################################
sub pgExecute
{
    my $strSql = shift;
    my $hDbLocal = shift;
    my $bRaiseError = shift;

    $bRaiseError = defined($bRaiseError) ? $bRaiseError : true;

    # Log the statement
    &log("     SQL: ${strSql}");

    # Execute the statement
    ($hDbLocal ? $hDbLocal : $hDb)->do($strSql);

    if ($DBI::errstr)
    {
        &log('          ' . ($bRaiseError ? 'got expected error' : 'unable to execute') . ': ' . $DBI::errstr, $bRaiseError);
    }
}

####################################################################################################################################
# pgDrop
####################################################################################################################################
sub pgDrop
{
    my $strPath = shift;

    # Set default
    $strPath = defined($strPath) ? $strPath : $strTestPath;

    # Stop the cluster
    pgStop(true, $strPath);

    # Remove the directory
    commandExecute("rm -rf ${strTestPath}");
}

####################################################################################################################################
# pgCreate
####################################################################################################################################
sub pgCreate
{
    my $strPath = shift;

    # Set default
    $strPath = defined($strPath) ? $strPath : $strTestPath;

    commandExecute("${strPgSqlBin}/initdb -D ${strPath} -U ${strUser}" . ' -A trust > /dev/null');

    commandExecute("echo 'local all all trust' > ${strPath}/pg_hba.conf");
    commandExecute("echo 'host all all 127.0.0.1/32 md5' >> ${strPath}/pg_hba.conf");
}

####################################################################################################################################
# pgStop
####################################################################################################################################
sub pgStop
{
    my $bImmediate = shift;
    my $strPath = shift;

    # Set default
    $strPath = defined($strPath) ? $strPath : $strTestPath;
    $bImmediate = defined($bImmediate) ? $bImmediate : false;

    # Disconnect user session
    pgDisconnect();

    # If postmaster process is running then stop the cluster
    if (-e $strPath . '/postmaster.pid')
    {
        commandExecute("${strPgSqlBin}/pg_ctl stop -D ${strPath} -w -s -m " . ($bImmediate ? 'immediate' : 'fast'), true);
    }
}

####################################################################################################################################
# pgStart
####################################################################################################################################
sub pgStart
{
    my $strPath = shift;

    # Set default
    $strPath = defined($strPath) ? $strPath : $strTestPath;

    # Make sure postgres is not running
    if (-e $strPath . '/postmaster.pid')
    {
        confess "${strPath}/postmaster.pid exists, cannot start";
    }

    # Start the cluster
    commandExecute("${strPgSqlBin}/pg_ctl start -o \"" .
                   " -c port=${iPort}" .
                   " -c shared_preload_libraries='pgaudit'" .
                   " -c log_min_messages=notice" .
                   " -c log_error_verbosity=verbose" .
                   " -c log_connections=on" .
                   " -c log_destination=csvlog" .
                   ($bNotice ? " -c pgaudit.log_level=notice" : '') .
                   " -c pgaudit.role=auditor" .
                   " -c logging_collector=on" .
                   " -c log_rotation_age=15" .
                   " -c log_connections=on" .
                   " -c unix_socket_directories='/tmp'" .
                   "\" -D ${strPath} -l ${strPath}/postgresql.log -w -s");

    # Connect user session
    $hDb = pgConnect();
}

####################################################################################################################################
# pgPsql
####################################################################################################################################
sub pgPsql
{
    my $strOption = shift;
    my $bSuppressError = shift;

    commandExecute("${strPgSqlBin}/psql -p ${iPort} ${strOption} ${strDatabase}", $bSuppressError);
}

####################################################################################################################################
# Main
####################################################################################################################################
my $strBasePath = dirname(dirname(abs_path($0)));
my $strAnalyzeExe = "${strBasePath}/bin/pgaudit_analyze";
my $strSql;

&log("INIT:\n");

# Drop the old cluster, build the code, and create a new cluster
pgDrop();
pgCreate();
pgStart();

# Load the audit schema
pgPsql("-f ${strBasePath}/sql/audit.sql");

# Start pgaudit_analyze
my $pId = IPC::Open3::open3(undef, undef, undef,
                            "${strBasePath}/bin/pgaudit_analyze --port=${iPort} --socket-path=/tmp" .
                            " --log-file=${strTestPath}/pgaudit_analyze.log ${strTestPath}/pg_log");

use constant LOCALHOST => '127.0.0.1';

# Create test users and roles
use constant USER1 => 'user1';
use constant AUDITOR_ROLE => 'auditor';

pgExecute('create user ' . USER1 . " with password '" . USER1 . "'");
pgExecute('create role ' . AUDITOR_ROLE);

&log(undef, undef, true);

# Verify that successful logons are logged
#-------------------------------------------------------------------------------
&log("\nTEST: logon-success (logon success is tracked)\n");
my $hUserDb = pgConnect(USER1, USER1, LOCALHOST);

$strSql =
    "select last_success is null,\n" .
    "       last_failure is null,\n" .
    "       failures_since_last_success = 0\n" .
    "  from pgaudit.logon_info()";

pgQueryTest($strSql, $hUserDb);

pgDisconnect($hUserDb);
$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

$strSql =
    "select last_success is not null and last_success <= current_timestamp,\n" .
    "       last_failure is null,\n" .
    "       failures_since_last_success = 0\n" .
    "  from pgaudit.logon_info()";

pgQueryTest($strSql, $hUserDb);

pgDisconnect($hUserDb);

&log(undef, undef, true);

# Verify that failed logons are logged (and cleared on a successfuly logon)
#-------------------------------------------------------------------------------
&log("\nTEST: logon-fail (logon failures are tracked)\n");

# Test that a logon failure is correctly logged
pgConnect(USER1, 'bogus-password', LOCALHOST, undef, false);
pgConnect(USER1, 'another-bogus-password', LOCALHOST, undef, false);

$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

$strSql =
    "select last_success is not null and last_success <= current_timestamp,\n" .
    "       last_failure is not null and last_failure >= last_success,\n" .
    "       failures_since_last_success = 2\n" .
    "  from pgaudit.logon_info()";

pgQueryTest($strSql, $hUserDb);

pgDisconnect($hUserDb);

# Test that logon failures are cleared after another successful logon
$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

$strSql =
    "select last_success is not null and last_success <= current_timestamp,\n" .
    "       last_failure is null,\n" .
    "       failures_since_last_success = 0\n" .
    "  from pgaudit.logon_info()";

pgQueryTest($strSql, $hUserDb);

pgDisconnect($hUserDb);

&log(undef, undef, true);

# Verify that correct fields are logged with the audit record
#-------------------------------------------------------------------------------
&log("\nTEST: audit-record (select statement is logged with details)\n");

pgExecute('alter user ' . USER1 . " set pgaudit.log = 'read'");
pgExecute('alter user ' . USER1 . " set pgaudit.log_relation = on");
pgExecute('create table test_table (id int)');
pgExecute('grant select on test_table to user1');

$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

$strSql =
    "select count(*) = 0\n" .
    "  from test_table";

pgExecute($strSql, $hUserDb);

$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and user_name = '" . USER1 . "'\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'session'\n" .
    "   and class = 'read'\n" .
    "   and command = 'select'\n" .
    "   and object_type = 'table'\n" .
    "   and object_name = 'public.test_table'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Verify that a user cannot change audit settings
#-------------------------------------------------------------------------------
&log("\nTEST: audit-modify (user cannot change audit settings - " .
     "but superuser can)\n");

# A user can check their audit settings
$strSql =
    "select setting = 'read'\n" .
    "  from pg_settings\n" .
    " where name = 'pgaudit.log'";

pgQueryTest($strSql, $hUserDb);

# But not modify them
$strSql =
    "set pgaudit.log = 'read, write'";

pgExecute($strSql, $hUserDb, false);

# Superuser can change them of course
pgExecute($strSql);

pgDisconnect($hUserDb);

&log(undef, undef, true);

# Verify that a role change error is logged
#-------------------------------------------------------------------------------
&log("\nTEST: audit-role-error (role change errors are logged)\n");

# Set role auditing on
pgExecute('alter user ' . USER1 . " reset pgaudit.log");
pgExecute("set pgaudit.log = 'role'");

$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

# Create an error modifying a role
$strSql =
    'alter role ' . USER1 . ' nologin';

pgExecute($strSql, $hUserDb, false);

$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.log_event\n" .
    " where error_severity = 'error'\n" .
    "   and command = 'alter role'\n" .
    "   and query = '${strSql}'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Verify that a role change is logged
#-------------------------------------------------------------------------------
&log("\nTEST: audit-role-log (role changes are logged)\n");

# Alter a role
$strSql =
    'alter role ' . USER1 . ' createdb';

pgExecute($strSql);

# Make sure the alter was audited
$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'session'\n" .
    "   and class = 'role'\n" .
    "   and command = 'alter role'\n" .
    "   and substatement = '${strSql}'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Verify that users added to a role are logged
#-------------------------------------------------------------------------------
&log("\nTEST: audit-role-user (add user to a role is logged)\n");

pgExecute('create role test_group');

# Add a user to the test role
$strSql =
    'grant test_group to ' . USER1;

pgExecute($strSql);

$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'session'\n" .
    "   and class = 'role'\n" .
    "   and command = 'grant role'\n" .
    "   and substatement = '${strSql}'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Verify that ddl in do blocks is logged
#-------------------------------------------------------------------------------
&log("\nTEST: do-block-ddl (create dynamic table in do block)\n");

pgExecute("set pgaudit.log = 'ddl'");

# Create table
$strSql =
    "do \$\$\n" .
    "begin\n" .
    "    execute 'CREATE TABLE import' || 'ant_table (id INT)';\n" .
    "end \$\$";

pgExecute($strSql);

# Verify table creation was logged
$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'session'\n" .
    "   and class = 'ddl'\n" .
    "   and command = 'create table'\n" .
    "   and object_type = 'table'\n" .
    "   and object_name = 'public.important_table'\n" .
    "   and substatement = 'CREATE TABLE important_table (id INT)'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Verify that object audit logging works
#-------------------------------------------------------------------------------
&log("\nTEST: object-audit (test object audit logging)\n");

pgExecute("set pgaudit.log = 'none'");
pgExecute("create table audit_table (id int)");
pgExecute("create table no_audit_table (id int)");
pgExecute("grant select on audit_table to auditor");

$hUserDb = pgConnect(USER1, USER1, LOCALHOST);

# Create table
$strSql =
    "select *\n" .
    "  from audit_table\n" .
    "       cross join no_audit_table";

pgExecute($strSql);

# Verify that select on audit_table was logged
$strSql =
    "select count(*) = 1\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'object'\n" .
    "   and class = 'read'\n" .
    "   and command = 'select'\n" .
    "   and object_type = 'table'\n" .
    "   and object_name = 'public.audit_table'\n" .
    "   and substatement = '${strSql}'";

pgQueryTest($strSql);

# Verify that select on no_audit_table was not logged
$strSql =
    "select count(*) = 0\n" .
    "  from pgaudit.vw_audit_event\n" .
    " where log_time is not null and log_time <= current_timestamp\n" .
    "   and state = 'ok'\n" .
    "   and audit_type = 'object'\n" .
    "   and class = 'read'\n" .
    "   and command = 'select'\n" .
    "   and object_type = 'table'\n" .
    "   and object_name = 'public.no_audit_table'";

pgQueryTest($strSql);

&log(undef, undef, true);

# Cleanup
#-------------------------------------------------------------------------------
# Stop the database
if (!$bNoCleanup)
{
    pgDrop();
}

# Send kill to pgaudit_analyze
kill 'KILL', $pId;
waitpid($pId, 0);

# Print success
&log("\nTESTS COMPLETED SUCCESSFULLY!");
