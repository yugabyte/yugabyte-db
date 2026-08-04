#-------------------------------------------------------------------------------
# Regression tests for the pgaudit "pgaudit stack is not empty" stack check in
# pgaudit_ProcessUtility_hook().
#
# Some commands legitimately leave an audit-stack entry behind when they run in
# a portal that outlives the statement -- this happens when a client uses the
# extended query protocol with a row limit (the JDBC setFetchSize(n>0)
# scenario), which leaves the portal *suspended*.  The next top-level utility
# statement then walks the stack and must not raise "pgaudit stack is not
# empty" for those entries.
#
# libpq (and therefore psql, pg_regress and DBD::Pg) always sends Execute with
# a row limit of 0, so it can never suspend a portal and cannot reproduce any
# of this.  We therefore speak the v3 frontend/backend protocol directly over
# the socket.
#
# Each test below opens its own connection so the scenarios are independent.
#-------------------------------------------------------------------------------

use strict;
use warnings FATAL => 'all';
use Test::More;

use IO::Socket::UNIX;
use IO::Socket::INET;
use PostgreSQL::Test::Cluster;

# Minimal v3 wire protocol helpers
#-------------------------------------------------------------------------------
# Build a typed frontend message (type byte + Int32 length + payload).
sub fe_msg
{
    my ($type, $payload) = @_;

    $payload = '' unless defined $payload;

    return $type . pack('N', 4 + length($payload)) . $payload;
}

# StartupMessage has no type byte and carries the protocol version.
sub startup_msg
{
    my (%params) = @_;
    my $payload = pack('N', 3 << 16);    # protocol 3.0

    $payload .= "$_\0$params{$_}\0" for sort keys %params;
    $payload .= "\0";

    return pack('N', 4 + length($payload)) . $payload;
}

# Simple Query message (one SQL string, simple query protocol).
sub query_msg { return fe_msg('Q', "$_[0]\0"); }

# Parse message: prepare a statement (name, SQL) declaring no parameter types.
sub parse_msg { return fe_msg('P', "$_[0]\0$_[1]\0" . pack('n', 0)); }

# Bind message: bind a prepared statement to a named portal.
sub bind_msg
{
    my ($portal, $stmt) = @_;

    # portal, stmt, 0 param formats, 0 params, 0 result formats
    return fe_msg('B', "$portal\0$stmt\0" . pack('n', 0) . pack('n', 0) . pack('n', 0));
}

# Execute message: run a portal, returning at most "max rows" rows (0 = all).
sub execute_msg { return fe_msg('E', "$_[0]\0" . pack('N', $_[1])); }

# Sync message: finish the extended-query exchange (server replies ReadyForQuery).
sub sync_msg { return fe_msg('S'); }

# Terminate message: end the session.
sub terminate_msg { return fe_msg('X'); }

# Read exactly $n bytes or die.
sub read_n
{
    my ($sock, $n) = @_;
    my $buf = '';

    while (length($buf) < $n)
    {
        my $r = sysread($sock, my $chunk, $n - length($buf));

        die "unexpected EOF from server" if !defined $r || $r == 0;
        $buf .= $chunk;
    }

    return $buf;
}

# Read one backend message, returning (type, payload).
sub read_msg
{
    my ($sock) = @_;
    my $type = read_n($sock, 1);
    my $len = unpack('N', read_n($sock, 4));
    my $payload = $len > 4 ? read_n($sock, $len - 4) : '';

    return ($type, $payload);
}

# Extract the human-readable message (field 'M') from an ErrorResponse payload.
sub error_text
{
    my ($payload) = @_;

    for my $field (split /\0/, $payload)
    {
        return substr($field, 1) if length($field) && substr($field, 0, 1) eq 'M';
    }

    return '';
}

# Drain messages until ReadyForQuery ('Z'); return (\@types, \@error_texts).
sub read_until_ready
{
    my ($sock) = @_;
    my (@types, @errors);

    while (1)
    {
        my ($type, $payload) = read_msg($sock);

        push @types, $type;
        push @errors, error_text($payload) if $type eq 'E';

        last if $type eq 'Z';
    }

    return (\@types, \@errors);
}

# Open a raw socket to the node and complete startup (trust auth, no password).
sub pg_connect
{
    my ($node) = @_;
    my $host = $node->host;
    my $sock;

    if (substr($host, 0, 1) eq '/')
    {
        $sock = IO::Socket::UNIX->new(Peer => "$host/.s.PGSQL." . $node->port)
          or die "could not connect to unix socket: $!";
    }
    else
    {
        $sock = IO::Socket::INET->new(PeerHost => $host, PeerPort => $node->port)
          or die "could not connect to $host: $!";
    }

    $sock->autoflush(1);
    binmode($sock);

    print $sock startup_msg(user => 'postgres', database => 'postgres');
    read_until_ready($sock);

    return $sock;
}

# Bring up a single node with pgaudit
#-------------------------------------------------------------------------------
my $node = PostgreSQL::Test::Cluster->new('audit');

$node->init;
$node->append_conf('postgresql.conf', "shared_preload_libraries = 'pgaudit'");
$node->append_conf('postgresql.conf', "pgaudit.log = 'all'");
# Keep audit output on stderr so $node->log_contains() can see it.
$node->append_conf('postgresql.conf', "logging_collector = off");
$node->start;

# One scenario per command tag that pgaudit_ProcessUtility_hook() allows to
# remain on the stack.  Each statement returns more than one row, so the
# row-limited Execute below suspends its portal and leaves the tag behind.
#-------------------------------------------------------------------------------
my @tests = (
    {
        name => 'SELECT',
        stmt => 'SELECT * FROM pg_class',
        log  => qr/AUDIT:.*,SELECT,/,
    },
    {
        name => 'SHOW',
        stmt => 'SHOW all',
        log  => qr/AUDIT:.*,SHOW,/,
    },
    {
        name => 'EXPLAIN',
        stmt => 'EXPLAIN SELECT g FROM generate_series(1, 5) g ORDER BY g',
        log  => qr/AUDIT:.*,EXPLAIN,/,
    },
    {
        name  => 'CALL',
        setup => [
            q{CREATE PROCEDURE pcursor(INOUT result refcursor) LANGUAGE plpgsql AS }
              . q{$proc$ BEGIN OPEN result FOR SELECT generate_series(1, 5); END $proc$}
        ],
        stmt => q{CALL pcursor('cc')},
        log  => qr/AUDIT:.*,CALL,/,
    },
    {
        name  => 'FETCH',
        setup => ['DECLARE c CURSOR FOR SELECT generate_series(1, 5)'],
        stmt  => 'FETCH ALL FROM c',
        log   => qr/AUDIT:.*,FETCH,/,
    },
);

# Run each scenario on its own connection: optionally set up state, run the
# statement over the extended protocol with a row limit of 1 so its portal is
# left suspended (leaving its command tag on the audit stack), then confirm a
# following top-level utility does not raise "pgaudit stack is not empty".
for my $test (@tests)
{
    # Connect and begin transaction
    my $name = $test->{name};
    my $sock = pg_connect($node);
    my $offset = -s $node->logfile;

    print $sock query_msg('BEGIN');
    read_until_ready($sock);

    # Optional setup inside the transaction (declare a cursor, create a
    # procedure that opens one, ...).
    for my $sql (@{ $test->{setup} // [] })
    {
        print $sock query_msg($sql);
        read_until_ready($sock);
    }

    # A named portal plus a non-zero row limit leaves the portal suspended,
    # exactly as JDBC's setFetchSize(n>0) does.
    print $sock parse_msg('', $test->{stmt});
    print $sock bind_msg('p_stack', '');
    print $sock execute_msg('p_stack', 1);
    print $sock sync_msg();

    my ($types, $errors) = read_until_ready($sock);

    ok((grep { $_ eq 's' } @$types), "$name leaves the portal suspended")
      or diag("$name reply messages: " . join(',', @$types));
    is_deeply($errors, [], "$name itself raised no error")
      or diag("$name errors: " . join(' | ', @$errors));

    # CLOSE ALL is a top-level utility statement: with the suspended statement
    # still on the audit stack this is where pgaudit raised the error.
    print $sock query_msg('CLOSE ALL');

    my (undef, $close_errors) = read_until_ready($sock);

    is_deeply($close_errors, [],
        "CLOSE after a suspended $name does not raise \"pgaudit stack is not empty\"")
      or diag("$name close errors: " . join(' | ', @$close_errors));

    # Commit and disconnect
    print $sock query_msg('COMMIT');
    read_until_ready($sock);
    print $sock terminate_msg();
    close($sock);

    # Check log
    ok($node->log_contains($test->{log}, $offset), "$name was audit logged");
}

# Cleanup
#-------------------------------------------------------------------------------
$node->stop;

done_testing();
