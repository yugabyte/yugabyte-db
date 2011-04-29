pgTAP 0.26.0
============

pgTAP is a unit testing framework for PostgreSQL written in PL/pgSQL and
PL/SQL. It includes a comprehensive collection of
[TAP](http://testanything.org)-emitting assertion functions, as well as the
ability to integrate with other TAP-emitting test frameworks. It can also be
used in the xUnit testing style.

Synopsis
========

    SELECT plan( 23 );
    -- or SELECT * from no_plan();

    -- Various ways to say "ok"
    SELECT ok( :have = :want, :test_description );

    SELECT is(   :have, :want, $test_description );
    SELECT isnt( :have, :want, $test_description );

    -- Rather than \echo # here's what went wrong
    SELECT diag( 'here''s what went wrong' );

    -- Compare values with LIKE or regular expressions.
    SELECT alike(   :have, :like_expression, $test_description );
    SELECT unalike( :have, :like_expression, $test_description );

    SELECT matches(      :have, :regex, $test_description );
    SELECT doesnt_match( :have, :regex, $test_description );

    SELECT cmp_ok(:have, '=', :want, $test_description );

    -- Skip tests based on runtime conditions.
    SELECT CASE WHEN :some_feature THEN collect_tap(
        ok( foo(),       :test_description),
        is( foo(42), 23, :test_description)
    ) ELSE skip(:why, :how_many ) END;

    -- Mark some tests as to-do tests.
    SELECT todo(:why, :how_many);
    SELECT ok( foo(),       :test_description);
    SELECT is( foo(42), 23, :test_description);

    -- Simple pass/fail.
    SELECT pass(:test_description);
    SELECT fail(:test_description);

Installation
============

For the impatient, to install pgTAP into a PostgreSQL database, just do this:

    make
    make install
    make installcheck

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as
'gmake':

    gmake
    gmake install
    gmake installcheck

If you encounter an error such as:

    make: pg_config: Command not found

Or:

    Makefile:52: *** pgTAP requires PostgreSQL 8.0 or later. This is .  Stop.

Be sure that you have `pg_config` installed and in your path. If you used a
package management system such as RPM to install PostgreSQL, be sure that the
`-devel` package is also installed. If necessary tell the build process where
to find it:

    env PG_CONFIG=/path/to/pg_config make && make install && make installcheck

And finally, if all that fails (and if you're on PostgreSQL 8.1 or lower, it
likely will), copy the entire distribution directory to the `contrib/`
subdirectory of the PostgreSQL source tree and try it there without
`pg_config`:

    env NO_PGXS=1 make && make install && make installcheck

If you encounter an error such as:

    ERROR:  must be owner of database regression

You need to run the test suite using a super user, such as the default
"postgres" super user:

    make installcheck PGUSER=postgres

If you want to schema-qualify pgTAP (that is, install all of its functions
into their own schema), set the `$TAPSCHEMA` variable to the name of the
schema you'd like to be created, for example:

    make TAPSCHEMA=tap
    make install
    make installcheck

Testing pgTAP with pgTAP
------------------------

In addition to the PostgreSQL-standard `installcheck` target, the `test`
target uses the `pg_prove` Perl program to do its testing, which will be
installed with the
[TAP::Parser::SourceHandler::pgTAP](http://search.cpan.org/dist/TAP-Parser-SourceHandler-pgTAP)
CPAN distribution. You'll need to make sure that you use a database with
PL/pgSQL loaded, or else the tests won't work. `pg_prove` supports a number of
environment variables that you might need to use, including all the usual
PostgreSQL client environment variables:

* `$PGDATABASE`
* `$PGHOST`
* `$PGPORT`
* `$PGUSER`

You can use it to run the test suite as a database super user like so:

    make test PGUSER=postgres

Adding pgTAP to a Database
--------------------------

Once pgTAP has been built and tested, you can install it into a
PL/pgSQL-enabled database:

    psql -d dbname -f pgtap.sql

If you want pgTAP to be available to all new databases, install it into the
"template1" database:

    psql -d template1 -f pgtap.sql

If you want to remove pgTAP from a database, run the `uninstall_pgtap.sql`
script:

    psql -d dbname -f uninstall_pgtap.sql

Both scripts will also be installed in the `contrib` directory under the
directory output by `pg_config --sharedir`. So you can always do this:

    psql -d template1 -f `pg_config --sharedir`/contrib/pgtap.sql

But do be aware that, if you've specified a schema using `$TAPSCHEMA`, that
schema will always be created and the pgTAP functions placed in it.

pgTAP Test Scripts
==================

You can distribute `pgtap.sql` with any PostgreSQL distribution, such as a
custom data type. For such a case, if your users want to run your test suite
using PostgreSQL's standard `installcheck` make target, just be sure to set
variables to keep the tests quiet, start a transaction, load the functions in
your test script, and then rollback the transaction at the end of the script.
Here's an example:

    \set ECHO
    \set QUIET 1
    -- Turn off echo and keep things quiet.

    -- Format the output for nice TAP.
    \pset format unaligned
    \pset tuples_only true
    \pset pager

    -- Revert all changes on failure.
    \set ON_ERROR_ROLLBACK 1
    \set ON_ERROR_STOP true
    \set QUIET 1

    -- Load the TAP functions.
    BEGIN;
        \i pgtap.sql

    -- Plan the tests.
    SELECT plan(1);

    -- Run the tests.
    SELECT pass( 'My test passed, w00t!' );

    -- Finish the tests and clean up.
    SELECT * FROM finish();
    ROLLBACK;

Of course, if you already have the pgTAP functions in your testing database,
you should skip `\i pgtap.sql` at the beginning of the script.

The only other limitation is that the `pg_typoeof()` function, which is
written in C, will not be available in 8.3 and lower. You'll want to
comment-out its declaration in the bundled copy of `pgtap.sql` and then avoid
using `cmp_ok()`, since that function relies on `pg_typeof()`. Note that
`pg_typeof()` is included in PostgreSQL 8.4, so ou won't need to avoid it on
that version or higher.

Now you're ready to run your test script!

    % psql -d try -Xf test.sql
    1..1
    ok 1 - My test passed, w00t!

You'll need to have all of those variables in the script to ensure that the
output is proper TAP and that all changes are rolled back -- including the
loading of the test functions -- in the event of an uncaught exception.

Using `pg_prove`
----------------

Or save yourself some effort -- and run a batch of tests scripts or all of
your xUnit test functions at once -- by using `pg_prove`, available in the
[TAP::Parser::SourceHandler::pgTAP](http://search.cpan.org/dist/TAP-Parser-SourceHandler-pgTAP)
CPAN distribution . If you're not relying on `installcheck`, your test scripts
can be a lot less verbose; you don't need to set all the extra variables,
because `pg_prove` takes care of that for you:

    -- Start transaction and plan the tests.
    BEGIN;
    SELECT plan(1);

    -- Run the tests.
    SELECT pass( 'My test passed, w00t!' );

    -- Finish the tests and clean up.
    SELECT * FROM finish();
    ROLLBACK;

Now run the tests. Here's what it looks like when the pgTAP tests are run with
`pg_prove`:

    % pg_prove -U postgres sql/*.sql
    sql/coltap.....ok
    sql/hastap.....ok
    sql/moretap....ok
    sql/pg73.......ok
    sql/pktap......ok
    All tests successful.
    Files=5, Tests=216,  1 wallclock secs ( 0.06 usr  0.02 sys +  0.08 cusr  0.07 csys =  0.23 CPU)
    Result: PASS

If you're using xUnit tests and just want to have `pg_prove` run them all
through the `runtests()` function, just tell it to do so:

    % pg_prove -d myapp --runtests

Yep, that's all there is to it. Call `pg_prove --help` to see other supported
options, and `pg_prove --man` to see its entire documentation.

Using pgTAP
===========

The purpose of pgTAP is to provide a wide range of testing utilities that
output TAP. TAP, or the "Test Anything Protocol", is an emerging standard for
representing the output from unit tests. It owes its success to its format as
a simple text-based interface that allows for practical machine parsing and
high legibility for humans. TAP started life as part of the test harness for
Perl but now has implementations in C/C++, Python, PHP, JavaScript, Perl, and
now PostgreSQL.

There are two ways to use pgTAP: 1) In simple test scripts that use a plan to
describe the tests in the script; or 2) In xUnit-style test functions that you
install into your database and run all at once in the PostgreSQL client of
your choice.

I love it when a plan comes together
------------------------------------

Before anything else, you need a testing plan. This basically declares how
many tests your script is going to run to protect against premature failure.

The preferred way to do this is to declare a plan by calling the `plan()`
function:

    SELECT plan( 42 );

There are rare cases when you will not know beforehand how many tests your
script is going to run. In this case, you can declare that you have no plan.
(Try to avoid using this as it weakens your test.)

    SELECT * FROM no_plan();

Often, though, you'll be able to calculate the number of tests, like so:

    SELECT plan( COUNT(*) )
      FROM foo;

At the end of your script, you should always tell pgTAP that the tests have
completed, so that it can output any diagnostics about failures or a
discrepancy between the planned number of tests and the number actually run:

    SELECT * FROM finish();

What a sweet unit!
------------------

If you're used to xUnit testing frameworks and using PostgreSQL 8.1 or higher,
you can collect all of your tests into database functions and run them all at
once with `runtests()`. This is similar to how
[PGUnit](http://en.dklab.ru/lib/dklab_pgunit/) and
[Epic](http://www.epictest.org/) work. The `runtests()` function does all the
work of finding and running your test functions in individual transactions. It
even supports setup and teardown functions. To use it, write your unit test
functions so that they return a set of text results, and then use the pgTAP
assertion functions to return TAP values, like so:

    CREATE OR REPLACE FUNCTION setup_insert(
    ) RETURNS SETOF TEXT AS $$
        RETURN NEXT is( MAX(nick), NULL, 'Should have no users') FROM users;
        INSERT INTO users (nick) VALUES ('theory');
    $$ LANGUAGE plpgsql;

    CREATE OR REPLACE FUNCTION test_user(
    ) RETURNS SETOF TEXT AS $$
       SELECT is( nick, 'theory', 'Should have nick') FROM users;
    END;
    $$ LANGUAGE sql;

See below for details on the pgTAP assertion functions. Once you've defined
your unit testing functions, you can run your tests at any time using the
`runtests()` function:

    SELECT * FROM runtests();

Each test function will run within its own transaction, and rolled back when
the function completes (or after any teardown functions have run). The TAP
results will be sent to your client.

Test names
----------

By convention, each test is assigned a number in order. This is largely done
automatically for you. However, it's often very useful to assign a name to
each test. Would you rather see this?

      ok 4
      not ok 5
      ok 6

Or this?

      ok 4 - basic multi-variable
      not ok 5 - simple exponential
      ok 6 - force == mass * acceleration

The latter gives you some idea of what failed. It also makes it easier to find
the test in your script, simply search for "simple exponential".

All test functions take a name argument. It's optional, but highly suggested
that you use it.
  
Sometimes it's useful to extract test function names from pgtap output, especially when using xUnit style with Continuous Integration Server like Hudson or TeamCity.
By default pgTAP displays this names as "comment", but you're able to change this behavior by overriding function `test_started`:

### `test_started( test_name )` ###

    CREATE OR REPLACE FUNCTION test_started(TEXT)
    RETURNS TEXT AS $$
        SELECT 'test ' || $1 || '()';
    $$ LANGUAGE SQL;

This will show
    test my_example_test_function_name()
instead of
    # my_example_test_function_name()
This makes easy handling test name and differing test names from comments.

I'm ok, you're not ok
---------------------

The basic purpose of pgTAP--and of any TAP-emitting test framework, for that
matter--is to print out either "ok #" or "not ok #", depending on whether a
given test succeeded or failed. Everything else is just gravy.

All of the following functions return "ok" or "not ok" depending on whether
the test succeeded or failed.

### `ok( boolean, description )` ###
### `ok( boolean )` ###

    SELECT ok( :this = :that, :description );

This function simply evaluates any expression (`:this = :that` is just a
simple example) and uses that to determine if the test succeeded or failed. A
true expression passes, a false one fails. Very simple.

For example:

    SELECT ok( 9 ^ 2 = 81,    'simple exponential' );
    SELECT ok( 9 < 10,        'simple comparison' );
    SELECT ok( 'foo' ~ '^f',  'simple regex' );
    SELECT ok( active = true, name ||  widget active' )
      FROM widgets;

(Mnemonic:  "This is ok.")

The `description` is a very short description of the test that will be printed
out. It makes it very easy to find a test in your script when it fails and
gives others an idea of your intentions. The description is optional, but we
*very* strongly encourage its use.

Should an `ok()` fail, it will produce some diagnostics:

    not ok 18 - sufficient mucus
    #     Failed test 18: "sufficient mucus"

Furthermore, should the boolean test result argument be passed as a `NULL`
rather than `true` or `false`, `ok()` will assume a test failure and attach an
additional diagnostic:

    not ok 18 - sufficient mucus
    #     Failed test 18: "sufficient mucus"
    #     (test result was NULL)

### `is( anyelement, anyelement, description )` ###
### `is( anyelement, anyelement )` ###
### `isnt( anyelement, anyelement, description )` ###
### `isnt( anyelement, anyelement )` ###

    SELECT is(   :this, :that, :description );
    SELECT isnt( :this, :that, :description );

Similar to `ok()`, `is()` and `isnt()` compare their two arguments with `IS
NOT DISTINCT FROM` (`=`) AND `IS DISTINCT FROM` (`<>`) respectively and use
the result of that to determine if the test succeeded or failed. So these:

    -- Is the ultimate answer 42?
    SELECT is( ultimate_answer(), 42, 'Meaning of Life' );

    -- foo() doesn't return empty
    SELECT isnt( foo(), '', 'Got some foo' );

are similar to these:

    SELECT ok(   ultimate_answer() =  42, 'Meaning of Life' );
    SELECT isnt( foo()             <> '', 'Got some foo'    );

(Mnemonic: "This is that." "This isn't that.")

*Note:* Thanks to the use of the `IS [ NOT ] DISTINCT FROM` construct, `NULL`s
are not treated as unknowns by `is()` or `isnt()`. That is, if `:this` and
`:that` are both `NULL`, the test will pass, and if only one of them is
`NULL`, the test will fail.

So why use these test functions? They produce better diagnostics on failure.
`ok()` cannot know what you are testing for (beyond the description), but
`is()` and `isnt()` know what the test was and why it failed. For example this
test:

    \set foo '\'waffle\''
    \set bar '\'yarblokos\''
    SELECT is( :foo::text, :bar::text, 'Is foo the same as bar?' );

Will produce something like this:

    # Failed test 17:  "Is foo the same as bar?"
    #         have: waffle
    #         want: yarblokos

So you can figure out what went wrong without re-running the test.

You are encouraged to use `is()` and `isnt()` over `ok()` where possible. You
can even use them to compar records in PostgreSQL 8.4 and later:

    SELECT is( users.*, ROW(1, 'theory', true)::users )
      FROM users
     WHERE nick = 'theory'; 

### `matches( anyelement, regex, description )` ###
### `matches( anyelement, regex )` ###

    SELECT matches( :this, '^that', :description );

Similar to `ok()`, `matches()` matches `:this` against the regex `/^that/`.

So this:

    SELECT matches( :this, '^that', 'this is like that' );

is similar to:

    SELECT ok( :this ~ '^that', 'this is like that' );

(Mnemonic "This matches that".)

Its advantages over `ok()` are similar to that of `is()` and `isnt()`: Better
diagnostics on failure.

### `imatches( anyelement, regex, description )` ###
### `imatches( anyelement, regex )` ###

    SELECT imatches( :this, '^that', :description );

These are just like `matches()` except that the regular expression is compared
to `:this` case-insensitively.

### `doesnt_match( anyelement, regex, description )` ###
### `doesnt_match( anyelement, regex )` ###
### `doesnt_imatch( anyelement, regex, description )` ###
### `doesnt_imatch( anyelement, regex )` ###

    SELECT doesnt_match( :this, '^that', :description );

These functions work exactly as `matches()` and `imatches()` do, only they
check if `:this` *does not* match the given pattern.

### `alike( anyelement, pattern, description )` ###
### `alike( anyelement, pattern )` ###
### `ialike( anyelement, pattern, description )` ###
### `ialike( anyelement, pattern )` ###

    SELECT alike( :this, 'that%', :description );

Similar to `matches()`, `alike()` matches `:this` against the SQL `LIKE`
pattern 'that%'. `ialike()` matches case-insensitively.

So this:

    SELECT ialike( :this, 'that%', 'this is alike that' );

is similar to:

    SELECT ok( :this ILIKE 'that%', 'this is like that' );

(Mnemonic "This is like that".)

Its advantages over `ok()` are similar to that of `is()` and `isnt()`: Better
diagnostics on failure.

### `unalike( anyelement, pattern, description )` ###
### `unalike( anyelement, pattern )` ###
### `unialike( anyelement, pattern, description )` ###
### `unialike( anyelement, pattern )` ###

    SELECT unalike( :this, 'that%', :description );

Works exactly as `alike()`, only it checks if `:this` *does not* match the
given pattern.

### `cmp_ok( anyelement, operator, anyelement, description )` ###
### `cmp_ok( anyelement, operator, anyelement )` ###

    SELECT cmp_ok( :this, :op, :that, :description );

Halfway between `ok()` and `is()` lies `cmp_ok()`. This function allows you to
compare two arguments using any binary operator.

    -- ok( :this = :that );
    SELECT cmp_ok( :this, '=', :that, 'this = that' );

    -- ok( :this >= :that );
    SELECT cmp_ok( :this, '>=, 'this >= that' );

    -- ok( :this && :that );
    SELECT cmp_ok( :this, '&&', :that, 'this && that' );

Its advantage over `ok()` is that when the test fails you'll know what `:this`
and `:that` were:

    not ok 1
    #     Failed test 1:
    #     '23'
    #         &&
    #     NULL

Note that if the value returned by the operation is `NULL`, the test will
be considered to have failed. This may not be what you expect if your test
was, for example:

    SELECT cmp_ok( NULL, '=', NULL );

But in that case, you should probably use `is()`, instead.

### `pass( description )` ###
### `pass()` ###
### `fail( description )` ###
### `fail()` ###

    SELECT pass( :description );
    SELECT fail( :description );

Sometimes you just want to say that the tests have passed. Usually the case is
you've got some complicated condition that is difficult to wedge into an
`ok()`. In this case, you can simply use `pass()` (to declare the test ok) or
`fail()` (for not ok). They are synonyms for `ok(1)` and `ok(0)`.

Use these functions very, very, very sparingly.

### `isa_ok( value, regtype, name )` ###
### `isa_ok( value, regtype )` ###

    SELECT isa_ok( :value, :regtype, name );

Checks to see if the given value is of a particular type. The description and
diagnostics of this test normally just refer to "the value". If you'd like
them to be more specific, you can supply a `:name`. For example you might say
"the return value" when yo're examing the result of a function call:

    SELECT isa_ok( length('foo'), 'integer', 'The return value from length()' );

In which case the description will be "The return value from length() isa
integer".

In the event of a failure, the diagnostic message will tell you what the type
of the value actually is:

    not ok 12 - the value isa integer[]
    #     the value isn't a "integer[]" it's a "boolean"

Pursuing Your Query
===================

Sometimes, you've just gotta test a query. I mean the results of a full blown
query, not just the scalar assertion functions we've seen so far. pgTAP
provides a number of functions to help you test your queries, each of which
takes one or two SQL statements as arguments. For example:

    SELECT throws_ok('SELECT divide_by(0)');

Yes, as strings. Of course, you'll often need to do something complex in your
SQL, and quoting SQL in strings in what is, after all, an SQL application, is
an unnecessary PITA. Each of the query-executing functions in this section
thus support an alternative to make your tests more SQLish: using prepared
statements.

[Prepared statements](http://www.postgresql.org/docs/current/static/sql-prepare.html
"PostgreSQL Documentation: PREPARE") allow you to just write SQL and simply
pass the prepared statement names to test functions. For example, the above
example can be rewritten as:

    PREPARE mythrow AS SELECT divide_by(0);
    SELECT throws_ok('mythrow');

pgTAP assumes that an SQL argument without space characters or starting with a
double quote character is a prepared statement and simply `EXECUTE`s it. If
you need to pass arguments to a prepared statement, perhaps because you plan
to use it in multiple tests to return different values, just `EXECUTE` it
yourself. Here's an example with a prepared statement with a space in its
name, and one where arguments need to be passed:

    PREPARE "my test" AS SELECT * FROM active_users() WHERE name LIKE 'A%';
    PREPARE expect AS SELECT * FROM users WHERE active = $1 AND name LIKE $2;

    SELECT results_eq(
        '"my test"',
        'EXECUTE expect( true, ''A%'' )'
    );

Since "my test" was declared with double quotes, it must be passed with double
quotes. And since the call to "expect" included spaces (to keep it legible),
the `EXECUTE` keyword was required.

In PostgreSQL 8.2 and up, you can also use a `VALUES` statement, both in
the query string or in a prepared statement. A useless example:

    PREPARE myvals AS VALUES (1, 2), (3, 4);
    SELECT set_eq(
        'myvals',
        'VALUES (1, 2), (3, 4)'
    );

On PostgreSQL 8.1 and down, you'll have to use `UNION` queries (if order
doesn't matter) or temporary tables to pass arbitrary values:

    SET client_min_messages = warning;
    CREATE TEMPORARY TABLE stuff (a int, b int);
    INSERT INTO stuff VALUES(1, 2);
    INSERT INTO stuff VALUES(3, 4);
    RESET client_min_messages;
    PREPARE myvals AS SELECT a, b FROM stuff;

    SELECT set_eq(
        'myvals',
        'SELECT 1, 2 UNION SELECT 3, 4'
    );

Here's a bonus if you need to check the results from a query that returns a
single column: for those functions that take two query arguments, the second
can be an array. Check it out:

    SELECT results_eq(
        'SELECT * FROM active_user_ids()',
        ARRAY[ 2, 3, 4, 5]
    );

The first query *must* return only one column of the same type as the values
in the array. If you need to test more columns, you'll need to use two
queries.

Keeping these techniques in mind, read on for all of the query-testing
goodness.

To Error is Human
-----------------

Sometimes you just want to know that a particular query will trigger an error.
Or maybe you want to make sure a query *does not* trigger an error. For such
cases, we provide a couple of test functions to make sure your queries are as
error-prone as you think they should be.

### `throws_ok( sql, errcode, errmsg, description )` ###
### `throws_ok( sql, errcode, errmsg )` ###
### `throws_ok( sql, errmsg, description )` ###
### `throws_ok( sql, errcode )` ###
### `throws_ok( sql, errmsg )` ###
### `throws_ok( sql )` ###

    PREPARE my_thrower AS INSERT INTO try (id) VALUES (1);
    SELECT throws_ok(
        'my_thrower',
        '23505',
        'duplicate key value violates unique constraint "try_pkey"',
        'We should get a unique violation for a duplicate PK'
    );

When you want to make sure that an exception is thrown by PostgreSQL, use
`throws_ok()` to test for it. Supported by 8.1 and up.

The first argument should be the name of a prepared statement or else a string
representing the query to be executed (see the [summary](#Pursuing+Your+Query)
for query argument details). `throws_ok()` will use the PL/pgSQL `EXECUTE`
statement to execute the query and catch any exception.

The second argument should be an exception error code, which is a
five-character string (if it happens to consist only of numbers and you pass
it as an integer, it will still work). If this value is not `NULL`,
`throws_ok()` will check the thrown exception to ensure that it is the
expected exception. For a complete list of error codes, see [Appendix
A.](http://www.postgresql.org/docs/current/static/errcodes-appendix.html
"Appendix A. PostgreSQL Error Codes") in the [PostgreSQL
documentation](http://www.postgresql.org/docs/current/static/).

The third argument is an error message. This will be most useful for functions
you've written that raise exceptions, so that you can test the exception
message that you've thrown. Otherwise, for core errors, you'll need to be
careful of localized error messages.  One trick to get around localized error
messages is to pass NULL as the third argument.  This allows you to still pass
a description as the fourth argument.

The fourth argument is of course a brief test description.

For the two- and three-argument forms of `throws_ok()`, if the second argument
is exactly five bytes long, it is assumed to be an error code and the optional
third argument is the error message. Otherwise, the second argument is assumed
to be an error message and the third argument is a description. If for some
reason you need to test an error message that is five bytes long, use the
four-argument form.

A failing `throws_ok()` test produces an appropriate diagnostic message. For
example:

    # Failed test 81: "This should die a glorious death"
    #       caught: 23505: duplicate key value violates unique constraint "try_pkey"
    #       wanted: 23502: null value in column "id" violates not-null constraint

Idea borrowed from the Test::Exception Perl module.

### `throws_like( query, pattern, description )` ###
### `throws_like( query, pattern )` ###

    PREPARE my_thrower AS INSERT INTO try (tz) VALUES ('America/Moscow');
    SELECT throws_like(
        'my_thrower',
        '%"timezone_check"',
        'We should error for invalid time zone'
    );

Like `throws_ok()`, but tests that an exception error message matches an SQL
`LIKE` pattern. Supported by 8.1 and up.

A failing `throws_like()` test produces an appropriate diagnostic message. For
example:

    # Failed test 85: "We should error for invalid time zone"
    #     error message: 'value for domain timezone violates check constraint "tz_check"'
    #     doesn't match: '%"timezone_check"'

### `throws_ilike( query, pattern, description )` ###
### `throws_ilike( query, pattern )` ###

    PREPARE my_thrower AS INSERT INTO try (tz) VALUES ('America/Moscow');
    SELECT throws_ilike(
        'my_thrower',
        '%"TZ_check"',
        'We should error for invalid time zone'
    );

Like `throws_like()`, but case-insensitively compares the exception message to
the SQL `LIKE` pattern.

### `throws_matching( query, regex, description )` ###
### `throws_matching( query, regex )` ###

    PREPARE my_thrower AS INSERT INTO try (tz) VALUES ('America/Moscow');
    SELECT throws_matching(
        'my_thrower',
        '.+"timezone_check"',
        'We should error for invalid time zone'
    );

Like `throws_ok()`, but tests that an exception error message matches a
regular expression. Supported by 8.1 and up.

A failing `throws_matching()` test produces an appropriate diagnostic message. For
example:

    # Failed test 85: "We should error for invalid time zone"
    #     error message: 'value for domain timezone violates check constraint "tz_check"'
    #     doesn't match: '.+"timezone_check"'

### `throws_imatching( query, regex, description )` ###
### `throws_imatching( query, regex )` ###

    PREPARE my_thrower AS INSERT INTO try (tz) VALUES ('America/Moscow');
    SELECT throws_imatching(
        'my_thrower',
        '.+"TZ_check"',
        'We should error for invalid time zone'
    );

Like `throws_matching()`, but case-insensitively compares the exception
message to the regular expression.

### `lives_ok( query, description )` ###
### `lives_ok( query )` ###

    SELECT lives_ok(
        'INSERT INTO try (id) VALUES (1)',
        'We should not get a unique violation for a new PK'
    );

The inverse of `throws_ok()`, `lives_ok()` ensures that an SQL statement does
*not* throw an exception. Supported by 8.1 and up. Pass in the name of a
prepared statement or string of SQL code (see the
[summary](#Pursuing+Your+Query) for query argument details). The optional
second argument is the test description.

A failing `lives_ok()` test produces an appropriate diagnostic message. For
example:

    # Failed test 85: "don't die, little buddy!"
    #         died: 23505: duplicate key value violates unique constraint "try_pkey"

Idea borrowed from the Test::Exception Perl module.

### `performs_ok( sql, milliseconds, description )` ###
### `performs_ok( sql, milliseconds )` ###

    PREPARE fast_query AS SELECT id FROM try WHERE name = 'Larry';
    SELECT performs_ok(
        'fast_query',
        250,
        'A select by name should be fast'
    );

This function makes sure that an SQL statement performs well. It does so by
timing its execution and failing if execution takes longer than the specified
number of milliseconds.

The first argument should be the name of a prepared statement or a string
representing the query to be executed (see the [summary](#Pursuing+Your+Query)
for query argument details). `throws_ok()` will use the PL/pgSQL `EXECUTE`
statement to execute the query.

The second argument is the maximum number of milliseconds it should take for
the SQL statement to execute. This argument is numeric, so you can even use
fractions of milliseconds if it floats your boat.

The third argument is the usual description. If not provided, `performs_ok()`
will generate the description "Should run in less than $milliseconds ms".
You'll likely want to provide your own description if you have more than a
couple of these in a test script or function.

Should a `performs_ok()` test fail it produces appropriate diagnostic
messages. For example:

    # Failed test 19: "The lookup should be fast!"
    #       runtime: 200.266 ms
    #       exceeds: 200 ms

*Note:* There is a little extra time included in the execution time for the
the overhead of PL/pgSQL's `EXECUTE`, which must compile and execute the SQL
string. You will want to account for this and pad your estimates accordingly.
It's best to think of this as a brute force comparison of runtimes, in order
to ensure that a query is not *really* slow (think seconds).

Can You Relate?
---------------

So you've got your basic scalar comparison functions, what about relations?
Maybe you have some pretty hairy `SELECT` statements in views or functions to
test? We've got your relation-testing functions right here.

### `results_eq( sql, sql, description )` ###
### `results_eq( sql, sql )` ###
### `results_eq( sql, array, description )` ###
### `results_eq( sql, array )` ###
### `results_eq( cursor, cursor, description )` ###
### `results_eq( cursor, cursor )` ###
### `results_eq( sql, cursor, description )` ###
### `results_eq( sql, cursor )` ###
### `results_eq( cursor, sql, description )` ###
### `results_eq( cursor, sql )` ###
### `results_eq( cursor, array, description )` ###
### `results_eq( cursor, array )` ###

    PREPARE users_test AS SELECT * FROM active_users();
    PREPARE users_expect AS
    VALUES ( 42, 'Anna'), (19, 'Strongrrl'),  (39, 'Theory');

    SELECT results_eq( 'users_test', 'users_expect', 'We should have users' );

There are three ways to test result sets in pgTAP. Perhaps the most intuitive
is to do a direct row-by-row comparison of results to ensure that they are
exactly what you expect, in the order you expect. Coincidentally, this is
exactly how `results_eq()` behaves. Here's how you use it: simply pass in two
SQL statements or prepared statement names (or some combination; (see the
[summary](#Pursuing+Your+Query) for query argument details) and an optional
description. Yep, that's it. It will do the rest.

For example, say that you have a function, `active_users()`, that returns a
set of rows from the users table. To make sure that it returns the rows you
expect, you might do something like this:

    SELECT results_eq(
        'SELECT * FROM active_users()',
        'SELECT * FROM users WHERE active',
        'active_users() should return active users'
    );

Tip: If you're using PostgreSQL 8.2 and up and want to hard-code the values to
compare, use a `VALUES` statement instead of a query, like so:

    SELECT results_eq(
        'SELECT * FROM active_users()',
        $$VALUES ( 42, 'Anna'), (19, 'Strongrrl'), (39, 'Theory')$$,
        'active_users() should return active users'
    );

If the results returned by the first argument consist of a single column, the
second argument may be an array:

    SELECT results_eq(
        'SELECT * FROM active_user_ids()',
        ARRAY[ 2, 3, 4, 5]
    );

In general, the use of prepared statements is highly recommended to keep your
test code SQLish (you can even use `VALUES` in prepared statements in
PostgreSQL 8.2 and up!). But note that, because `results_eq()` does a
row-by-row comparison, the results of the two query arguments must be in
exactly the same order, with exactly the same data types, in order to pass. In
practical terms, it means that you must make sure that your results are never
unambiguously ordered.

For example, say that you want to compare queries against a `persons` table.
The simplest way to sort is by `name`, as in:

    try=# select * from people order by name;
      name  | age 
    --------+-----
     Damian |  19
     Larry  |  53
     Tom    |  44
     Tom    |  35
    (4 rows)

But a different run of the same query could have the rows in different order:

    try=# select * from people order by name;
      name  | age 
    --------+-----
     Damian |  19
     Larry  |  53
     Tom    |  35
     Tom    |  44
    (4 rows)

Notice how the two "Tom" rows are reversed. The upshot is that you must ensure
that your queries are always fully ordered. In a case like the above, it means
sorting on both the `name` column and the `age` column. If the sort order of
your results isn't important, consider using `set_eq()` or `bag_eq()` instead.

Internally, `results_eq()` turns your SQL statements into cursors so that it
can iterate over them one row at a time. Conveniently, this behavior is
directly available to you, too. Rather than pass in some arbitrary SQL
statement or the name of a prepared statement, simply create a cursor and pass
*it* in, like so:

    DECLARE cwant CURSOR FOR SELECT * FROM active_users();
    DECLARE chave CURSOR FOR SELECT * FROM users WHERE active ORDER BY name;

    SELECT results_eq(
        'cwant'::refcursor,
        'chave'::refcursor,
        'Gotta have those active users!'
    );

The key is to ensure that the cursor names are passed as `refcursor`s. This
allows `results_eq()` to disambiguate them from prepared statements. And of
course, you can mix and match cursors, prepared statements, and SQL as much as
you like. Here's an example using a prepared statement and a (reset) cursor
for the expected results:

    PREPARE users_test AS SELECT * FROM active_users();
    MOVE BACKWARD ALL IN chave;

    SELECT results_eq(
        'users_test',
        'chave'::refcursor,
        'Gotta have those active users!'
    );

Regardless of which types of arguments you pass, in the event of a test
failure, `results_eq()` will offer a nice diagnostic message to tell you at
what row the results differ, something like:

    # Failed test 146
    #     Results differ beginning at row 3:
    #         have: (1,Anna)
    #         want: (22,Betty)

If there are different numbers of rows in each result set, a non-existent row
will be represented as "NULL":

    # Failed test 147
    #     Results differ beginning at row 5:
    #         have: (1,Anna)
    #         want: NULL

On PostgreSQL 8.4 or higher, if the number of columns varies between result
sets, or if results are of different data types, you'll get diagnostics like
so:

    # Failed test 148
    #     Column types differ between queries:
    #         have: (1)
    #         want: (foo,1)

On PostgreSQL 8.3 and down, the rows are cast to text for comparison, rather
than compared as `record` objects. The downside to this necessity is that the
test cannot detect incompatibilities in column numbers or types, or
differences in columns that convert to the same text representation. For
example, a `NULL` column will be equivalent to an empty string. The upshot:
read failure diagnostics carefully and pay attention to data types on 8.3 and
down.

### `results_ne( sql, sql, description )` ###
### `results_ne( sql, sql )` ###
### `results_ne( sql, array, description )` ###
### `results_ne( sql, array )` ###
### `results_ne( cursor, cursor, description )` ###
### `results_ne( cursor, cursor )` ###
### `results_ne( sql, cursor, description )` ###
### `results_ne( sql, cursor )` ###
### `results_ne( cursor, sql, description )` ###
### `results_ne( cursor, sql )` ###
### `results_ne( cursor, array, description )` ###
### `results_ne( cursor, array )` ###

    PREPARE users_test AS SELECT * FROM active_users();
    PREPARE not_users AS
    VALUES ( 42, 'Anna'), (19, 'Strongrrl'),  (39, 'Theory');

    SELECT results_ne( 'users_test', 'not_users', 'We should get only users' );

The inverse of `results_eq()`, this function tests that query results are not
equivalent. Note that, like `results_ne()`, order matters, so you can actually
have the same sets of results in the two query arguments and the test will
pass if they're merely in a different order. More than likely what you really
want is `results_eq()` or `set_ne()`. But this function is included for
completeness and is kind of cute, so enjoy. If a `results_ne()` test fails,
however, there will be no diagnostics, because, well, the results will be the
same!

Note that the caveats for `results_ne()` on PostgreSQL 8.3 and down apply to
`results_ne()` as well.

### `set_eq( sql, sql, description )` ###
### `set_eq( sql, sql )` ###
### `set_eq( sql, array, description )` ###
### `set_eq( sql, array )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE expect AS SELECT * FROM USERS where name LIKE 'a%';
    SELECT set_eq( 'testq', 'expect', 'gotta have the A listers' );

Sometimes you don't care what order query results are in, or if there are
duplicates. In those cases, use `set_eq()` to do a simple set comparison of
your result sets. As long as both queries return the same records, regardless
of duplicates or ordering, a `set_eq()` test will pass.

The SQL arguments can be the names of prepared statements or strings
containing an SQL query (see the [summary](#Pursuing+Your+Query) for query
argument details), or even one of each. If the results returned by the first
argument consist of a single column, the second argument may be an array:

    SELECT set_eq(
        'SELECT * FROM active_user_ids()',
        ARRAY[ 2, 3, 4, 5]
    );

In whatever case you choose to pass arguments, a failing test will yield
useful diagnostics, such as:

    # Failed test 146
    #     Extra records:
    #         (87,Jackson)
    #         (1,Jacob)
    #     Missing records:
    #         (44,Anna)
    #         (86,Angelina)

In the event that you somehow pass queries that return rows with different
types of columns, pgTAP will tell you that, too:

    # Failed test 147
    #     Columns differ between queries:
    #         have: (integer,text)
    #         want: (text,integer)

This of course extends to sets with different numbers of columns:

    # Failed test 148
    #     Columns differ between queries:
    #         have: (integer)
    #         want: (text,integer)

### `set_ne( sql, sql, description )` ###
### `set_ne( sql, sql )` ###
### `set_ne( sql, array, description )` ###
### `set_ne( sql, array )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE expect AS SELECT * FROM USERS where name LIKE 'b%';
    SELECT set_ne( 'testq', 'expect', 'gotta have the A listers' );

The inverse of `set_eq()`, this function tests that the results of two queries
are *not* the same. The two queries can as usual be the names of prepared
statements or strings containing an SQL query (see the
[summary](#Pursuing+Your+Query) for query argument details), or even one of
each. The two queries, however, must return results that are directly
comparable -- that is, with the same number and types of columns in the same
orders. If it happens that the query you're testing returns a single column,
the second argument may be an array.

### `set_has( sql, sql, description )` ###
### `set_has( sql, sql )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE subset AS SELECT * FROM USERS where name LIKE 'a%';
    SELECT set_has( 'testq', 'subset', 'gotta have at least the A listers' );

When you need to test that a query returns at least some subset of records,
`set_has()` is the hammer you're looking for. It tests that the the results of
a query contain at least the results returned by another query, if not more.
That is, the test passes if the second query's results are a subset of the
first query's results. The second query can even return an empty set, in which
case the test will pass no matter what the first query returns. Not very
useful perhaps, but set-theoretically correct.

As with `set_eq()`. the SQL arguments can be the names of prepared statements
or strings containing an SQL query (see the [summary](#Pursuing+Your+Query)
for query argument details), or one of each. If it happens that the query
you're testing returns a single column, the second argument may be an array.

In whatever case, a failing test will yield useful diagnostics just like:

    # Failed test 122
    #     Missing records:
    #         (44,Anna)
    #         (86,Angelina)

As with `set_eq()`, `set_has()` will also provide useful diagnostics when the
queries return incompatible columns. Internally, it uses an `EXCEPT` query to
determine if there any any unexpectedly missing results.

### `set_hasnt( sql, sql, description )` ###
### `set_hasnt( sql, sql )` ###

    PREPARE testq   AS SELECT * FROM users('a%');
    PREPARE exclude AS SELECT * FROM USERS where name LIKE 'b%';
    SELECT set_has( 'testq', 'exclude', 'Must not have the Bs' );

This test function is the inverse of `set_has()`: the test passes when the
results of the first query have none of the results of the second query.
Diagnostics are similarly useful:

    # Failed test 198
    #     Extra records:
    #         (44,Anna)
    #         (86,Angelina)

Internally, the function uses an `INTERSECT` query to determine if there is
any unexpected overlap between the query results.

### `bag_eq( sql, sql, description )` ###
### `bag_eq( sql, sql )` ###
### `bag_eq( sql, array, description )` ###
### `bag_eq( sql, array )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE expect AS SELECT * FROM USERS where name LIKE 'a%';
    SELECT bag_eq( 'testq', 'expect', 'gotta have the A listers' );

The `bag_eq()` function is just like `set_eq()`, except that it considers the
results as bags rather than as sets. A bag is a set that allows duplicates. In
practice, it mean that you can use `bag_eq()` to test result sets where order
doesn't matter, but duplication does. In other words, if a two rows are the
same in the first result set, the same row must appear twice in the second
result set.

Otherwise, this function behaves exactly like `set_eq()`, including the
utility of its diagnostics.

### `bag_ne( sql, sql, description )` ###
### `bag_ne( sql, sql )` ###
### `bag_ne( sql, array, description )` ###
### `bag_ne( sql, array )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE expect AS SELECT * FROM USERS where name LIKE 'b%';
    SELECT bag_ne( 'testq', 'expect', 'gotta have the A listers' );

The inverse of `bag_eq()`, this function tests that the results of two queries
are *not* the same, including duplicates. The two queries can as usual be the
names of prepared statements or strings containing an SQL query (see the
[summary](#Pursuing+Your+Query) for query argument details), or even one of
each. The two queries, however, must return results that are directly
comparable -- that is, with the same number and types of columns in the same
orders. If it happens that the query you're testing returns a single column,
the second argument may be an array.

### `bag_has( sql, sql, description )` ###
### `bag_has( sql, sql )` ###

    PREPARE testq  AS SELECT * FROM users('a%');
    PREPARE subset AS SELECT * FROM USERS where name LIKE 'a%';
    SELECT bag_has( 'testq', 'subset', 'gotta have at least the A listers' );

The `bag_has()` function is just like `set_has()`, except that it considers
the results as bags rather than as sets. A bag is a set with duplicates. What
practice this means that you can use `bag_has()` to test result sets where
order doesn't matter, but duplication does. Internally, it uses an `EXCEPT
ALL` query to determine if there any any unexpectedly missing results.

### `bag_hasnt( sql, sql, description )` ###
### `bag_hasnt( sql, sql )` ###

    PREPARE testq   AS SELECT * FROM users('a%');
    PREPARE exclude AS SELECT * FROM USERS where name LIKE 'b%';
    SELECT bag_has( 'testq', 'exclude', 'Must not have the Bs' );

This test function is the inverse of `bag_hasnt()`: the test passes when the
results of the first query have none of the results of the second query.
Diagnostics are similarly useful:

    # Failed test 198
    #     Extra records:
    #         (44,Anna)
    #         (86,Angelina)

Internally, the function uses an `INTERSECT ALL` query to determine if there
is any unexpected overlap between the query results. This means that a
duplicate row in the first query will appear twice in the diagnostics if it is
also duplicated in the second query.

### `is_empty( sql, description )` ###
### `is_empty( sql )` ###

    PREPARE emptyset  AS SELECT * FROM users(FALSE);
    SELECT is_empty( 'emptyset', 'Should have no inactive users' );

The `is_empty()` function takes a single query string or prepared statement
name as its first argument, and tests that said query returns no records.
Internally it simply executes the query and if there are any results, the test
fails and the results are displayed in the failure diagnostics, like so:

    # Failed test 494: "Should have no inactive users"
    #     Records returned:
    #         (1,Jacob,false)
    #         (2,Emily,false)

### `row_eq( sql, record, description )` ###
### `row_eq( sql, record )` ###

    PREPARE testrow  AS SELECT * FROM users where nick = 'theory';
    SELECT row_eq('testrow', ROW(1, 'theory', 'David Wheeler')::users);

Compares the contents of a single row to a record. Works on PostgreSQL 8.1 and
higher. Due to the limitations of non-C functions in PostgreSQL, a bar
`RECORD` value cannot be passed to the function. You must instead pass in a
valid composite type value, and cast the record argument (the second argument)
to the same type. Both explicitly created composite types and table types are
supported. Thus, you can do this:

    CREATE TYPE sometype AS (
        id    INT,
        name  TEXT
    );

    SELECT row_eq( ROW(1, 'foo')::sometype, ROW(1, 'foo')::sometype );

And, of course, thins:

    CREATE TABLE users (
        id   INT,
        name TEXT
    );

    INSERT INTO users VALUES (1, 'theory');

    SELECT row_eq( id, name), ROW(1, 'theory')::users )
      FROM users;

Compatible types can be compared, though. So if the `users` table actually
included an `active` column, for example, and you only wanted to test the
`id` and `name`, you could do this:

    SELECT row_eq( id, name), ROW(1, 'theory')::sometype )
      FROM users;

Note the use of the `sometype` composite type for the second argument. The
upshot is that you can create composite types in your tests explicitly for
comparing the rerutn values of your queries, if such queries don't return an
existing valid type.

Hopefully someday in the future we'll be able to support arbitrary `record`
arguments. In the meantime, this is the 90% solution.

Diagnostics on failure are similar to those from `is()`:

    # Failed test 322
    #       have: (1,Jacob)
    #       want: (1,Larry)

The Schema Things
=================

Need to make sure that your database is designed just the way you think it
should be? Use these test functions and rest easy.

A note on comparisons: pgTAP uses a simple equivalence test (`=`) to compare
all SQL identifiers, such as the names of tables, schemas, functions, indexes,
and columns (but not data types). So in general, you should always use
lowercase strings when passing identifier arguments to the functions below.
Use mixed case strings only when the objects were declared in your schema
using double-quotes. For example, if you created a table like so:

    CREATE TABLE Foo (id integer);

Then you *must* test for it using only lowercase characters (if you want the
test to pass):

    SELECT has_table('foo');

If, however, you declared the table using a double-quoted string, like so:

    CREATE TABLE "Foo" (id integer);

Then you'd need to test for it using exactly the same string, including case,
like so:

    SELECT has_table('Foo');

In general, this should not be an issue, as mixed-case objects are created
only rarely. So if you just stick to lowercase-only arguments to these
functions, you should be in good shape.

I Object!
---------

In a busy development environment, you might have a number of users who make
changes to the database schema. Sometimes you have to really work to keep
these folks in line. For example, do they add objects to the database without
adding tests? Do they drop objects that they shouldn't? These assertions are
designed to help you ensure that the objects in the database are exactly the
objects that should be in the database, no more, no less.

Each tests tests that all of the objects in the database are only the objects
that *should* be there. In other words, given a list of objects, say tables in
a call to `tables_are()`, this assertion will fail if there are tables that
are not in the list, or if there are tables in the list that are missing from
the database. It can also be useful for testing replication and the success or
failure of schema change deployments.

If you're more interested in the specifics of particular objects, skip to
the next section.

### `tablespaces_are( tablespaces, description )` ###
### `tablespaces_are( tablespaces )` ###

    SELECT tablespaces_are(
        ARRAY[ 'dbspace', 'indexspace' ],
        'Should have the correct tablespaces'
    );

This function tests that all of the tablespaces in the database only the
tablespaces that *should* be there. In the event of a failure, you'll see
diagnostics listing the extra and/or missing tablespaces, like so:

    # Failed test 121: "There should be the correct tablespaces"
    #     Extra tablespaces:
    #         trigspace
    #     Missing tablespaces:
    #         indexspace

### `schemas_are( schemas, description )` ###
### `schemas_are( schemas )` ###

    SELECT schemas_are(
        ARRAY[ 'public', 'contrib', 'tap' ],
        'Should have the correct schemas'
    );

This function tests that all of the schemas in the database only the schemas
that *should* be there, excluding system schemas and `information_schema`. In
the event of a failure, you'll see diagnostics listing the extra and/or
missing schemas, like so:

    # Failed test 106: "There should be the correct schemas"
    #     Extra schemas:
    #         __howdy__
    #     Missing schemas:
    #         someschema

### `tables_are( schema, tables, description )` ###
### `tables_are( tables, description )` ###
### `tables_are( schema, tables )` ###
### `tables_are( tables )` ###

    SELECT tables_are(
        'myschema',
        ARRAY[ 'users', 'widgets', 'gadgets', 'session' ],
        'Should have the correct tables in myschema'
    );

This function tests that all of the tables in the named schema, or that are
visible in the search path, are only the tables that *should* be there. If the
`:schema` argument is omitted, tables will be sought in the search path,
excluding `pg_catalog` and `information_schema` If the description is omitted,
a generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing tables, like so:

    # Failed test 91: "Schema public should have the correct tables"
    #     Extra tables:
    #         mallots
    #         __test_table
    #     Missing tables:
    #         users
    #         widgets

### `views_are( schema, views, description )` ###
### `views_are( views, description )` ###
### `views_are( schema, views )` ###
### `views_are( views )` ###

    SELECT views_are(
        'myschema',
        ARRAY[ 'users', 'widgets', 'gadgets', 'session' ],
        'Should have the correct views in myschema'
    );

This function tests that all of the views in the named schema, or that are
visible in the search path, are only the views that *should* be there. If the
`:schema` argument is omitted, views will be sought in the search path,
excluding `pg_catalog` and `information_schema` If the description is omitted,
a generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing views, like so:

    # Failed test 92: "Schema public should have the correct views"
    #     Extra views:
    #         v_userlog_tmp
    #         __test_view
    #     Missing views:
    #         v_userlog
    #         eated

### `sequences_are( schema, sequences, description )` ###
### `sequences_are( sequences, description )` ###
### `sequences_are( schema, sequences )` ###
### `sequences_are( sequences )` ###

    SELECT sequences_are(
        'myschema',
        ARRAY[ 'users', 'widgets', 'gadgets', 'session' ],
        'Should have the correct sequences in myschema'
    );

This function tests that all of the sequences in the named schema, or that are
visible in the search path, are only the sequences that *should* be there. If
the `:schema` argument is omitted, sequences will be sought in the search
path, excluding `pg_catalog` and `information_schema`. If the description is
omitted, a generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing sequences, like so:

    # Failed test 93: "Schema public should have the correct sequences"
    #     These are extra sequences:
    #         seq_mallots
    #         __test_table_seq
    #     These sequences are missing:
    #         users_seq
    #         widgets_seq

### `columns_are( schema, table, columns[], description )` ###
### `columns_are( schema, table, columns[] )` ###
### `columns_are( table, columns[], description )` ###
### `columns_are( table, columns[] )` ###

    SELECT columns_are(
        'myschema',
        'atable',
        ARRAY[ 'id', 'name', 'rank', 'sn' ],
        'Should have the correct columns on myschema.atable'
    );

This function tests that all of the columns on the named table are only the
columns that *should* be on that table. If the `:schema` argument is omitted,
the table must be visible in the search path, excluding `pg_catalog` and
`information_schema`. If the description is omitted, a generally useful
default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing columns, like so:

    # Failed test 183: "Table users should have the correct columns"
    #     Extra columns:
    #         given_name
    #         surname
    #     Missing columns:
    #         name

### `indexes_are( schema, table, indexes[], description )` ###
### `indexes_are( schema, table, indexes[] )` ###
### `indexes_are( table, indexes[], description )` ###
### `indexes_are( table, indexes[] )` ###

    SELECT indexes_are(
        'myschema',
        'atable',
        ARRAY[ 'atable_pkey', 'idx_atable_name' ],
        'Should have the correct indexes on myschema.atable'
    );

This function tests that all of the indexes on the named table are only the
indexes that *should* be on that table. If the `:schema` argument is omitted,
the table must be visible in the search path, excluding `pg_catalog` and
`information_schema`. If the description is omitted, a generally useful
default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing indexes, like so:

    # Failed test 180: "Table fou should have the correct indexes"
    #     Extra indexes:
    #         fou_pkey
    #     Missing indexes:
    #         idx_fou_name

### `triggers_are( schema, table, triggers[], description )` ###
### `triggers_are( schema, table, triggers[] )` ###
### `triggers_are( table, triggers[], description )` ###
### `triggers_are( table, triggers[] )` ###

    SELECT triggers_are(
        'myschema',
        'atable',
        ARRAY[ 'atable_pkey', 'idx_atable_name' ],
        'Should have the correct triggers on myschema.atable'
    );

This function tests that all of the triggers on the named table are only the
triggers that *should* be on that table. If the `:schema` argument is omitted,
the table must be visible in the search path, excluding `pg_catalog` and
`information_schema`. If the description is omitted, a generally useful
default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing triggers, like so:

    # Failed test 180: "Table fou should have the correct triggers"
    #     Extra triggers:
    #         set_user_pass
    #     Missing triggers:
    #         set_users_pass

### `functions_are( schema, functions[], description )` ###
### `functions_are( schema, functions[] )` ###
### `functions_are( functions[], description )` ###
### `functions_are( functions[] )` ###

    SELECT functions_are(
        'myschema',
        ARRAY[ 'foo', 'bar', 'frobnitz' ],
        'Should have the correct functions in myschema'
    );

This function tests that all of the functions in the named schema, or that are
visible in the search path, are only the functions that *should* be there. If
the `:schema` argument is omitted, functions will be sought in the search
path, excluding `pg_catalog` and `information_schema` If the description is
omitted, a generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing functions, like so:

    # Failed test 150: "Schema someschema should have the correct functions"
    #     Extra functions:
    #         schnauzify
    #     Missing functions:
    #         frobnitz

### `roles_are( roles[], description )` ###
### `roles_are( roles[] )` ###

    SELECT roles_are(
        ARRAY[ 'postgres', 'someone', 'root' ],
        'Should have the correct roles'
    );

This function tests that all of the roles in the database only the roles that
*should* be there. Supported in PostgreSQL 8.1 and higher. In the event of a
failure, you'll see diagnostics listing the extra and/or missing roles, like
so:

    # Failed test 195: "There should be the correct roles"
    #     Extra roles:
    #         root
    #     Missing roles:
    #         bobby

### `users_are( users[], description )` ###
### `users_are( users[] )` ###

    SELECT users_are(
        ARRAY[ 'postgres', 'someone', 'root' ],
        'Should have the correct users'
    );

This function tests that all of the users in the database only the users that
*should* be there. In the event of a failure, you'll see diagnostics listing
the extra and/or missing users, like so:

    # Failed test 195: "There should be the correct users"
    #     Extra users:
    #         root
    #     Missing users:
    #         bobby

### `groups_are( groups[], description )` ###
### `groups_are( groups[] )` ###

    SELECT groups_are(
        ARRAY[ 'postgres', 'admins, 'l0s3rs' ],
        'Should have the correct groups'
    );

This function tests that all of the groups in the database only the groups
that *should* be there. In the event of a failure, you'll see diagnostics
listing the extra and/or missing groups, like so:

    # Failed test 210: "There should be the correct groups"
    #     Extra groups:
    #         meanies
    #     Missing groups:
    #         __howdy__

### `languages_are( languages[], description )` ###
### `languages_are( languages[] )` ###

    SELECT languages_are(
        ARRAY[ 'plpgsql', 'plperl', 'pllolcode' ],
        'Should have the correct procedural languages'
    );

This function tests that all of the procedural languages in the database only
the languages that *should* be there. In the event of a failure, you'll see
diagnostics listing the extra and/or missing languages, like so:

    # Failed test 225: "There should be the correct procedural languages"
    #     Extra languages:
    #         pllolcode
    #     Missing languages:
    #         plpgsql

### `opclasses_are( schema, opclasses[], description )` ###
### `opclasses_are( schema, opclasses[] )` ###
### `opclasses_are( opclasses[], description )` ###
### `opclasses_are( opclasses[] )` ###

    SELECT opclasses_are(
        'myschema',
        ARRAY[ 'foo', 'bar', 'frobnitz' ],
        'Should have the correct opclasses in myschema'
    );

This function tests that all of the operator classes in the named schema, or
that are visible in the search path, are only the opclasses that *should* be
there. If the `:schema` argument is omitted, opclasses will be sought in the
search path, excluding `pg_catalog` and `information_schema`. If the
description is omitted, a generally useful default description will be
generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing opclasses, like so:

    # Failed test 251: "Schema public should have the correct operator classes"
    #     Extra operator classes:
    #         goofy_ops
    #     Missing operator classes:
    #         custom_ops

### `rules_are( schema, table, rules[], description )` ###
### `rules_are( schema, table, rules[] )` ###
### `rules_are( table, rules[], description )` ###
### `rules_are( table, rules[] )` ###

    SELECT rules_are(
        'myschema',
        'atable',
        ARRAY[ 'on_insert', 'on_update', 'on_delete' ],
        'Should have the correct rules on myschema.atable'
    );

This function tests that all of the rules on the named relation are only the
rules that *should* be on that relation (a table or a view). If the `:schema`
argument is omitted, the rules must be visible in the search path, excluding
`pg_catalog` and `information_schema`. If the description is omitted, a
generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing rules, like so:

    # Failed test 281: "Relation public.users should have the correct rules"
    #     Extra rules:
    #         on_select
    #     Missing rules:
    #         on_delete

### `types_are( schema, types[], description )` ###
### `types_are( schema, types[] )` ###
### `types_are( types[], description )` ###
### `types_are( types[] )` ###

    SELECT types_are(
        'myschema',
        ARRAY[ 'timezone', 'state' ],
        'Should have the correct types in myschema'
    );

Tests that all of the types in the named schema are the only types in that
schema, including base types, composite types, domains, enums, and
pseudo-types. If the `:schema` argument is omitted, the types must be visible
in the search path, excluding `pg_catalog` and `information_schema`. If the
description is omitted, a generally useful default description will be
generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing types, like so:

    # Failed test 307: "Schema someschema should have the correct types"
    #     Extra types:
    #         sometype
    #     Missing types:
    #         timezone

### `domains_are( schema, domains[], description )` ###
### `domains_are( schema, domains[] )` ###
### `domains_are( domains[], description )` ###
### `domains_are( domains[] )` ###

    SELECT domains_are(
        'myschema',
        ARRAY[ 'timezone', 'state' ],
        'Should have the correct domains in myschema'
    );

Tests that all of the domains in the named schema are the only domains in that
schema. If the `:schema` argument is omitted, the domains must be visible in
the search path, excluding `pg_catalog` and `information_schema`. If the
description is omitted, a generally useful default description will be
generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing domains, like so:

    # Failed test 327: "Schema someschema should have the correct domains"
    #     Extra domains:
    #         somedomain
    #     Missing domains:
    #         timezone

### `enums_are( schema, enums[], description )` ###
### `enums_are( schema, enums[] )` ###
### `enums_are( enums[], description )` ###
### `enums_are( enums[] )` ###

    SELECT enums_are(
        'myschema',
        ARRAY[ 'timezone', 'state' ],
        'Should have the correct enums in myschema'
    );

Tests that all of the enums in the named schema are the only enums in that
schema. Enums are supported in PostgreSQL 8.3 and up. If the `:schema`
argument is omitted, the enums must be visible in the search path, excluding
`pg_catalog` and `information_schema`. If the description is omitted, a
generally useful default description will be generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing enums, like so:

    # Failed test 333: "Schema someschema should have the correct enums"
    #     Extra enums:
    #         someenum
    #     Missing enums:
    #         bug_status

### `casts_are( casts[], description )` ###
### `casts_are( casts[] )` ###

    SELECT casts_are(
        ARRAY[
            'integer AS double precision',
            'integer AS reltime',
            'integer AS numeric',
            -- ...
        ],
        'Should have the correct casts'
    );

This function tests that all of the casts in the database are only the casts
that *should* be in that database. Casts are specified as strings in a syntax
similarly to how they're declared via `CREATE CAST`. The pattern is
`:source_type AS :target_type`. If either type was created with double-quotes
to force mixed case or special characers, then you must use double quotes in
the cast strings:

    SELECT casts_are(
        ARRAY[
            'integer AS "myInteger"',
            -- ...
        ]
    );


If the description is omitted, a generally useful default description will be
generated.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing casts, like so:

    # Failed test 302: "There should be the correct casts"
    #     Extra casts:
    #         lseg AS point
    #     Missing casts:
    #         lseg AS integer

### `operators_are( schema, operators[], description )` ###
### `operators_are( schema, operators[] )` ###
### `operators_are( operators[], description )` ###
### `operators_are( operators[] )` ###

    SELECT operators_are(
        'public',
        ARRAY[
            '=(citext,citext) RETURNS boolean',
            '-(NONE,bigint) RETURNS bigint',
            '!(bigint,NONE) RETURNS numeric',
            -- ...
        ],
        ''
    );

Tests that all of the operators in the named schema are the only operators in
that schema. If the `:schema` argument is omitted, the operators must be
visible in the search path, excluding `pg_catalog` and `information_schema`.
If the description is omitted, a generally useful default description will be
generated.

The `:operators` argument is specified as an array of strings in which
each operator is defined similarly to the display of the `:regoperator` type.
The format is `:op(:leftop,:rightop) RETURNS :return_type`.

For left operators the left argument type should be `NONE`. For right
operators, the right argument type should be `NONE`. The example above shows
one one of each of the operator types. `=(citext,citext)` is an infix
operator, `-(bigint,NONE)` is a left operator, and `!(NONE,bigint)` is a right
operator.

In the event of a failure, you'll see diagnostics listing the extra and/or
missing operators, like so:

    # Failed test 453: "Schema public should have the correct operators"
    #     Extra operators:
    #         +(integer,integer) RETURNS integer
    #     Missing enums:
    #         +(integer,text) RETURNS text

To Have or Have Not
-------------------

Perhaps you're not so concerned with ensuring the [precise correlation of
database objects](#I+Object! "I Object!"). Perhaps you just need to make sure
that certain objects exist (or that certain objects *don't* exist). You've
come to the right place.

### `has_tablespace( tablespace, location, description )` ###
### `has_tablespace( tablespace, description )` ###
### `has_tablespace( tablespace )` ###

    SELECT has_tablespace(
        'sometablespace',
        '/data/dbs',
        'I got sometablespace in /data/dbs'
    );

This function tests whether or not a tablespace exists in the database. The
first argument is a tablespace name. The second is either the a file system
path for the database or a test description. If you specify a location path,
you must pass a description as the third argument; otherwise, if you omit the
test description, it will be set to "Tablespace `:tablespace` should exist".

### `hasnt_tablespace( tablespace, tablespace, description )` ###
### `hasnt_tablespace( tablespace, description )` ###
### `hasnt_tablespace( tablespace )` ###

    SELECT hasnt_tablespace(
        'sometablespace',
        'There should be no tablespace sometablespace'
    );

This function is the inverse of `has_tablespace()`. The test passes if the
specified tablespace does *not* exist.

### `has_schema( schema, description )` ###
### `has_schema( schema )` ###

    SELECT has_schema(
        'someschema',
        'I got someschema'
    );

This function tests whether or not a schema exists in the database. The first
argument is a schema name and the second is the test description. If you omit
the schema, the schema must be visible in the search path. If you omit the
test description, it will be set to "Schema `:schema` should exist".

### `hasnt_schema( schema, description )` ###
### `hasnt_schema( schema )` ###

    SELECT hasnt_schema(
        'someschema',
        'There should be no schema someschema'
    );

This function is the inverse of `has_schema()`. The test passes if the
specified schema does *not* exist.

### `has_table( schema, table, description )` ###
### `has_table( table, description )` ###
### `has_table( table )` ###

    SELECT has_table(
        'myschema',
        'sometable',
        'I got myschema.sometable'
    );

This function tests whether or not a table exists in the database. The first
argument is a schema name, the second is a table name, and the third is the
test description. If you omit the schema, the table must be visible in the
search path. If you omit the test description, it will be set to "Table
`:table` should exist".

### `hasnt_table( schema, table, description )` ###
### `hasnt_table( table, description )` ###
### `hasnt_table( table )` ###

    SELECT hasnt_table(
        'myschema',
        'sometable',
        'There should be no table myschema.sometable'
    );

This function is the inverse of `has_table()`. The test passes if the
specified table does *not* exist.

### `has_view( schema, view, description )` ###
### `has_view( view, description )` ###
### `has_view( view )` ###

    SELECT has_view(
        'myschema',
        'someview',
        'I got myschema.someview'
    );

Just like `has_table()`, only it tests for the existence of a view.

### `hasnt_view( schema, view, description )` ###
### `hasnt_view( view, description )` ###
### `hasnt_view( view )` ###

    SELECT hasnt_view(
        'myschema',
        'someview',
        'There should be no myschema.someview'
    );

This function is the inverse of `has_view()`. The test passes if the specified
view does *not* exist.

### `has_sequence( schema, sequence, description )` ###
### `has_sequence( sequence, description )` ###
### `has_sequence( sequence )` ###

    SELECT has_sequence(
        'myschema',
        'somesequence',
        'I got myschema.somesequence'
    );

Just like `has_table()`, only it tests for the existence of a sequence.

### `hasnt_sequence( schema, sequence, description )` ###
### `hasnt_sequence( sequence, description )` ###
### `hasnt_sequence( sequence )` ###

    SELECT hasnt_sequence(
        'myschema',
        'somesequence',
        'There should be no myschema.somesequence'
    );

This function is the inverse of `has_sequence()`. The test passes if the
specified sequence does *not* exist.

### `has_type( schema, type, description )` ###
### `has_type( schema, type )` ###
### `has_type( type, description )` ###
### `has_type( type )` ###

    SELECT has_type(
        'myschema',
        'sometype',
        'I got myschema.sometype'
    );

This function tests whether or not a type exists in the database. Detects all
types of types, including base types, composite types, domains, enums, and
pseudo-types. The first argument is a schema name, the second is a type name,
and the third is the test description. If you omit the schema, the type must
be visible in the search path. If you omit the test description, it will be
set to "Type `:type` should exist". If you're passing a schema and type rather
than type and description, be sure to cast the arguments to `name` values so
that your type name doesn't get treated as a description.

If you've created a composite type and want to test that the composed types
are a part of it, use the column testing functions to verify them, like so:

    CREATE TYPE foo AS (id int, name text);
    SELECT has_type( 'foo' );
    SELECT has_column( 'foo', 'id' );
    SELECT col_type_is( 'foo', 'id', 'integer' );

### `hasnt_type( schema, type, description )` ###
### `hasnt_type( schema, type )` ###
### `hasnt_type( type, description )` ###
### `hasnt_type( type )` ###

    SELECT hasnt_type(
        'myschema',
        'sometype',
        'There should be no type myschema.sometype'
    );

This function is the inverse of `has_type()`. The test passes if the specified
type does *not* exist.

### `has_domain( schema, domain, description )` ###
### `has_domain( schema, domain )` ###
### `has_domain( domain, description )` ###
### `has_domain( domain )` ###

    SELECT has_domain(
        'myschema',
        'somedomain',
        'I got myschema.somedomain'
    );

This function tests whether or not a domain exists in the database. The first
argument is a schema name, the second is the name of a domain, and the third
is the test description. If you omit the schema, the domain must be visible in
the search path. If you omit the test description, it will be set to "Domain
`:domain` should exist". If you're passing a schema and domain rather than
domain and description, be sure to cast the arguments to `name` values so that
your domain name doesn't get treated as a description.

### `hasnt_domain( schema, domain, description )` ###
### `hasnt_domain( schema, domain )` ###
### `hasnt_domain( domain, description )` ###
### `hasnt_domain( domain )` ###

    SELECT hasnt_domain(
        'myschema',
        'somedomain',
        'There should be no domain myschema.somedomain'
    );

This function is the inverse of `has_domain()`. The test passes if the
specified domain does *not* exist.

### `has_enum( schema, enum, description )` ###
### `has_enum( schema, enum )` ###
### `has_enum( enum, description )` ###
### `has_enum( enum )` ###

    SELECT has_enum(
        'myschema',
        'someenum',
        'I got myschema.someenum'
    );

This function tests whether or not a enum exists in the database. Enums are
supported in PostgreSQL 8.3 or higher. The first argument is a schema name,
the second is the an enum name, and the third is the test description. If you
omit the schema, the enum must be visible in the search path. If you omit the
test description, it will be set to "Enum `:enum` should exist". If you're
passing a schema and enum rather than enum and description, be sure to cast
the arguments to `name` values so that your enum name doesn't get treated as a
description.

### `hasnt_enum( schema, enum, description )` ###
### `hasnt_enum( schema, enum )` ###
### `hasnt_enum( enum, description )` ###
### `hasnt_enum( enum )` ###

    SELECT hasnt_enum(
        'myschema',
        'someenum',
        'I don''t got myschema.someenum'
    );

This function is the inverse of `has_enum()`. The test passes if the specified
enum does *not* exist.

### `has_index( schema, table, index, columns[], description )` ###
### `has_index( schema, table, index, columns[] )` ###
### `has_index( schema, table, index, column/expression, description )` ###
### `has_index( schema, table, index, columns/expression )` ###
### `has_index( table, index, columns[], description )` ###
### `has_index( table, index, columns[], description )` ###
### `has_index( table, index, column/expression, description )` ###
### `has_index( schema, table, index, column/expression )` ###
### `has_index( table, index, column/expression )` ###
### `has_index( schema, table, index )` ###
### `has_index( table, index, description )` ###
### `has_index( table, index )` ###

    SELECT has_index(
        'myschema',
        'sometable',
        'myindex',
        ARRAY[ 'somecolumn', 'anothercolumn' ],
        'Index "myindex" should exist'
    );

    SELECT has_index('myschema', 'sometable', 'anidx', 'somecolumn');
    SELECT has_index('myschema', 'sometable', 'loweridx', 'LOWER(somecolumn)');
    SELECT has_index('sometable', 'someindex');

Checks for the existence of an index associated with the named table. The
`:schema` argument is optional, as is the column name or names or expression,
and the description. The columns argument may be a string naming one column or
an array of column names. It may also be a string representing an expression,
such as `lower(foo)`. For expressions, you must use lowercase for all SQL
keywords and functions to properly compare to PostgreSQL's internal form of
the expression.

If you find that the function call seems to be getting confused, cast the
index name to the `NAME` type:

    SELECT has_index( 'public', 'sometab', 'idx_foo', 'name'::name );

If the index does not exist, `has_index()` will output a diagnostic message
such as:

    # Index "blah" ON public.sometab not found

If the index was found but the column specification or expression is
incorrect, the diagnostics will look more like this:

    #       have: "idx_baz" ON public.sometab(lower(name))
    #       want: "idx_baz" ON public.sometab(lower(lname))

### `hasnt_index( schema, table, index, description )` ###
### `hasnt_index( schema, table, index )` ###
### `hasnt_index( table, index, description )` ###
### `hasnt_index( table, index )` ###

    SELECT hasnt_index(
        'myschema',
        'sometable',
        'someindex',
        'Index "someindex" should not exist'
    );

This function is the inverse of `has_index()`. The test passes if the
specified index does *not* exist.

### `has_trigger( schema, table, trigger, description )` ###
### `has_trigger( schema, table, trigger )` ###
### `has_trigger( table, trigger, description )` ###
### `has_trigger( table, trigger )` ###

    SELECT has_trigger(
        'myschema',
        'sometable',
        'sometrigger',
        'Trigger "sometrigger" should exist'
    );

    SELECT has_trigger( 'sometable', 'sometrigger' );

Tests to see if the specified table has the named trigger. The `:description`
is optional, and if the schema is omitted, the table with which the trigger is
associated must be visible in the search path.

### `hasnt_trigger( schema, table, trigger, description )` ###
### `hasnt_trigger( schema, table, trigger )` ###
### `hasnt_trigger( table, trigger, description )` ###
### `hasnt_trigger( table, trigger )` ###

    SELECT hasnt_trigger(
        'myschema',
        'sometable',
        'sometrigger',
        'Trigger "sometrigger" should not exist'
    );

This function is the inverse of `has_trigger()`. The test passes if the
specified trigger does *not* exist.

### `has_rule( schema, table, rule, description )` ###
### `has_rule( schema, table, rule )` ###
### `has_rule( table, rule, description )` ###
### `has_rule( table, rule )` ###

    SELECT has_rule(
        'myschema',
        'sometable',
        'somerule,
        'Rule "somerule" should exist'
    );

    SELECT has_rule( 'sometable', 'somerule' );

Tests to see if the specified table has the named rule. The `:description` is
optional, and if the schema is omitted, the table with which the rule is
associated must be visible in the search path.

### `hasnt_rule( schema, table, rule, description )` ###
### `hasnt_rule( schema, table, rule )` ###
### `hasnt_rule( table, rule, description )` ###
### `hasnt_rule( table, rule )` ###

    SELECT hasnt_rule( 'sometable', 'somerule' );

This function is the inverse of `has_rule()`. The test passes if the specified
rule does *not* exist.

### `has_function( schema, function, args[], description )` ###
### `has_function( schema, function, args[] )` ###
### `has_function( schema, function, description )` ###
### `has_function( schema, function )` ###
### `has_function( function, args[], description )` ###
### `has_function( function, args[] )` ###
### `has_function( function, description )` ###
### `has_function( function )` ###

    SELECT has_function(
        'pg_catalog',
        'decode',
        ARRAY[ 'text', 'text' ],
        'Function decode(text, text) should exist'
    );

    SELECT has_function( 'do_something' );
    SELECT has_function( 'do_something', ARRAY['integer'] );
    SELECT has_function( 'do_something', ARRAY['numeric'] );

Checks to be sure that the given function exists in the named schema and with
the specified argument data types. If `:schema` is omitted, `has_function()` will
search for the function in the schemas defined in the search path. If
`:args[]` is omitted, `has_function()` will see if the function exists without
regard to its arguments.

The `:args[]` argument should be formatted as it would be displayed in the
view of a function using the `\df` command in `psql`. For example, even if you
have a numeric column with a precision of 8, you should specify
`ARRAY['numeric']`". If you created a `varchar(64)` column, you should pass
the `:args[]` argument as `ARRAY['character varying']`.

If you wish to use the two-argument form of `has_function()`, specifying only
the schema and the function name, you must cast the `:function` argument to
`:name` in order to disambiguate it from from the
`has_function(:function, :description)` form. If you neglect to do so, your
results will be unexpected.

Also, if you use the string form to specify the `:args[]` array, be sure to
cast it to `name[]` to disambiguate it from a text string:

    SELECT has_function( 'lower', '{text}'::name[] );

**Deprecation notice:** The old name for this test function, `can_ok()`, is
still available, but emits a warning when called. It will be removed in a
future version of pgTAP.

### `hasnt_function( schema, function, args[], description )` ###
### `hasnt_function( schema, function, args[] )` ###
### `hasnt_function( schema, function, description )` ###
### `hasnt_function( schema, function )` ###
### `hasnt_function( function, args[], description )` ###
### `hasnt_function( function, args[] )` ###
### `hasnt_function( function, description )` ###
### `hasnt_function( function )` ###

    SELECT hasnt_function(
        'pg_catalog',
        'alamode',
        ARRAY[ 'text', 'text' ],
        'Function alamode(text, text) should not exist'
    );

    SELECT hasnt_function( 'bogus' );
    SELECT hasnt_function( 'bogus', ARRAY['integer'] );
    SELECT hasnt_function( 'bogus', ARRAY['numeric'] );

This function is the inverse of `has_function()`. The test passes if the
specified function (optionally with the specified signature) does *not* exist.

### `has_cast( source_type, target_type, schema, function, description )` ###
### `has_cast( source_type, target_type, schema, function )` ###
### `has_cast( source_type, target_type, function, description )` ###
### `has_cast( source_type, target_type, function )` ###
### `has_cast( source_type, target_type, description )` ###
### `has_cast( source_type, target_type )` ###

    SELECT has_cast(
        'integer',
        'bigint',
        'pg_catalog',
        'int8'
        'We should have a cast from integer to bigint'
    );

Tests for the existence of a cast. A cast consists of a source data type, a
target data type, and perhaps a (possibly schema-qualified) function. If you
omit the description four the 3- or 4-argument version, you'll need to cast
the function name to the `NAME` data type so that PostgreSQL doesn't resolve
the function name as a description. For example:

    SELECT has_cast( 'integer', 'bigint', 'int8'::NAME );

pgTAP will generate a useful description if you don't provide one.

### `hasnt_cast( source_type, target_type, schema, function, description )` ###
### `hasnt_cast( source_type, target_type, schema, function )` ###
### `hasnt_cast( source_type, target_type, function, description )` ###
### `hasnt_cast( source_type, target_type, function )` ###
### `hasnt_cast( source_type, target_type, description )` ###
### `hasnt_cast( source_type, target_type )` ###

    SELECT hasnt_cast( 'integer', 'circle' );

This function is the inverse of `has_cast()`. The test passes if the specified
cast does *not* exist.

### `has_operator( left_type, schema, name, right_type, return_type, description )` ###
### `has_operator( left_type, schema, name, right_type, return_type )` ###
### `has_operator( left_type, name, right_type, return_type, description )` ###
### `has_operator( left_type, name, right_type, return_type )` ###
### `has_operator( left_type, name, right_type, description )` ###
### `has_operator( left_type, name, right_type )` ###

    SELECT has_operator(
        'integer',
        'pg_catalog', '<=',
        'integer',
        'boolean',
        'Operator (integer <= integer RETURNS boolean) should exist'
    );

Tests for the presence of a binary operator. If the operator exists with the
given schema, name, left and right arguments, and return value, the test will
fail. If the operator does not exist, the test will fail. If you omit the
schema name, then the operator must be visible in the search path. If you omit
the test description, pgTAP will generate a reasonable one for you. The return
value is also optional. If you need to test for a left or right unary
operator, use `has_leftop()` or `has_rightop()` instead.

### `has_leftop( schema, name, right_type, return_type, description )` ###
### `has_leftop( schema, name, right_type, return_type )` ###
### `has_leftop( name, right_type, return_type, description )` ###
### `has_leftop( name, right_type, return_type )` ###
### `has_leftop( name, right_type, description )` ###
### `has_leftop( name, right_type )` ###

    SELECT has_leftop(
        'pg_catalog', '!!',
         'bigint',
         'numeric',
         'Operator (!! bigint RETURNS numeric) should exist'
     );

Tests for the presence of a left-unary operator. If the operator exists with
the given schema, name, right argument, and return value, the test will fail.
If the operator does not exist, the test will fail. If you omit the schema
name, then the operator must be visible in the search path. If you omit the
test description, pgTAP will generate a reasonable one for you. The return
value is also optional.

### `has_rightop( left_type, schema, name, return_type, description )` ###
### `has_rightop( left_type, schema, name, return_type )` ###
### `has_rightop( left_type, name, return_type, description )` ###
### `has_rightop( left_type, name, return_type )` ###
### `has_rightop( left_type, name, description )` ###
### `has_rightop( left_type, name )` ###

    SELECT has_rightop(
        'bigint',
        'pg_catalog', '!',
        'numeric',
        'Operator (bigint ! RETURNS numeric) should exist'
    );

Tests for the presence of a right-unary operator. If the operator exists with
the given left argument, schema, name, and return value, the test will fail.
If the operator does not exist, the test will fail. If you omit the schema
name, then the operator must be visible in the search path. If you omit the
test description, pgTAP will generate a reasonable one for you. The return
value is also optional.

### `has_opclass( schema, name, description )` ###
### `has_opclass( schema, name )` ###
### `has_opclass( name, description )` ###
### `has_opclass( name )` ###

    SELECT has_opclass(
        'myschema',
        'my_ops',
        'We should have the "my_ops" operator class'
    );

Tests for the presence of an operator class. If you omit the schema name, then
the operator must be visible in the search path. If you omit the test
description, pgTAP will generate a reasonable one for you. The return value is
also optional.

### `hasnt_opclass( schema, name, description )` ###
### `hasnt_opclass( schema, name )` ###
### `hasnt_opclass( name, description )` ###
### `hasnt_opclass( name )` ###

    SELECT hasnt_opclass(
        'myschema',
        'your_ops',
        'We should not have the "your_ops" operator class'
    );

This function is the inverse of `has_opclass()`. The test passes if the
specified operator class does *not* exist.

### `has_role( role, description )` ###
### `has_role( role )` ###

    SELECT has_role( 'theory', 'Role "theory" should exist' );

Checks to ensure that a database role exists. If the description is omitted,
it will default to "Role `:role` should exist".

### `hasnt_role( role, description )` ###
### `hasnt_role( role )` ###

    SELECT hasnt_role( 'theory', 'Role "theory" should not exist' );

The inverse of `has_role()`, this function tests for the *absence* of a
database role.

### `has_user( user, description )` ###
### `has_user( user )` ###

    SELECT has_user( 'theory', 'User "theory" should exist' );

Checks to ensure that a database user exists. If the description is omitted,
it will default to "User `:user` should exist".

### `hasnt_user( user, description )` ###
### `hasnt_user( user )` ###

    SELECT hasnt_user( 'theory', 'User "theory" should not exist' );

The inverse of `has_user()`, this function tests for the *absence* of a
database user.

### `has_group( group, description )` ###
### `has_group( group )` ###

    SELECT has_group( 'sweeties, 'Group "sweeties" should exist' );

Checks to ensure that a database group exists. If the description is omitted,
it will default to "Group `:group` should exist".

### `hasnt_group( group, description )` ###
### `hasnt_group( group )` ###

    SELECT hasnt_group( 'meanies, 'Group meaines should not exist' );

The inverse of `has_group()`, this function tests for the *absence* of a
database group.

### `has_language( language, description )` ###
### `has_language( language )` ###

    SELECT has_language( 'plpgsql', 'Language "plpgsql" should exist' );

Checks to ensure that a procedural language exists. If the description is
omitted, it will default to "Procedural language `:language` should exist".

### `hasnt_language( language, description )` ###
### `hasnt_language( language )` ###

    SELECT hasnt_language( 'plpgsql', 'Language "plpgsql" should not exist' );

The inverse of `has_language()`, this function tests for the *absence* of a
procedural language.

Table For One
-------------

Okay, you're sure that your database has exactly the [right schema](#I+Object!
"I Object!") and that all of the objects you need [are
there](#To+Have+or+Have+Not "To Have or Have Not"). So let's take a closer
look at tables. There are a lot of ways to look at tables, to make sure that
they have all the columns, indexes, constraints, keys, and indexes they need.
So we have the assertions to validate 'em.

### `has_column( schema, table, column, description )` ###
### `has_column( table, column, description )` ###
### `has_column( table, column )` ###

    SELECT has_column(
        'myschema',
        'sometable',
        'somecolumn',
        'I got myschema.sometable.somecolumn'
    );

Tests whether or not a column exists in a given table, view, or composite
type. The first argument is the schema name, the second the table name, the
third the column name, and the fourth is the test description. If the schema
is omitted, the table must be visible in the search path. If the test
description is omitted, it will be set to "Column `:table.:column` should
exist".

### `hasnt_column( schema, table, column, description )` ###
### `hasnt_column( table, column, description )` ###
### `hasnt_column( table, column )` ###

    SELECT hasnt_column(
        'myschema',
        'sometable',
        'somecolumn',
        'There should be no myschema.sometable.somecolumn column'
    );

This function is the inverse of `has_column()`. The test passes if the
specified column does *not* exist in the specified table, view, or composite
type.

### `col_not_null( schema, table, column, description )` ###
### `col_not_null( table, column, description )` ###
### `col_not_null( table, column )` ###

    SELECT col_not_null(
        'myschema',
        'sometable',
        'somecolumn',
        'Column myschema.sometable.somecolumn should be NOT NULL'
    );

Tests whether the specified column has a `NOT NULL` constraint. The first
argument is the schema name, the second the table name, the third the column
name, and the fourth is the test description. If the schema is omitted, the
table must be visible in the search path. If the test description is omitted,
it will be set to "Column `:table.:column` should be NOT NULL". Note that this
test will fail with a useful diagnostic message if the table or column in
question does not exist. But use `has_column()` to make sure the column exists
first, eh?

### `col_is_null( schema, table, column, description )` ###
### `col_is_null( table, column, description )` ###
### `col_is_null( table, column )` ###

    SELECT col_is_null(
        'myschema',
        'sometable',
        'somecolumn',
        'Column myschema.sometable.somecolumn should allow NULL'
    );

This function is the inverse of `col_not_null()`: the test passes if the
column does not have a `NOT NULL` constraint. The first argument is the schema
name, the second the table name, the third the column name, and the fourth is
the test description. If the schema is omitted, the table must be visible in
the search path. If the test description is omitted, it will be set to "Column
`:table.:column` should allow NULL". Note that this test will fail with a
useful diagnostic message if the table or column in question does not exist.
But use `has_column()` to make sure the column exists first, eh?

### `col_has_default( schema, table, column, description )` ###
### `col_has_default( table, column, description )` ###
### `col_has_default( table, column )` ###

    SELECT col_has_default(
        'myschema',
        'sometable',
        'somecolumn',
        'Column myschema.sometable.somecolumn has a default'
    );

Tests whether or not a column has a default value. Fails if the column doesn't
have a default value. It will also fail if the column doesn't exist, and emit
useful diagnostics to let you know:

    # Failed test 136: "desc"
    #     Column public.sometab.__asdfasdfs__ does not exist

### `col_hasnt_default( schema, table, column, description )` ###
### `col_hasnt_default( table, column, description )` ###
### `col_hasnt_default( table, column )` ###

    SELECT col_hasnt_default(
        'myschema',
        'sometable',
        'somecolumn',
        'There should be no default on myschema.sometable.somecolumn'
    );

This function is the inverse of `col_has_default()`. The test passes if the
specified column does *not* have a default. It will still fail if the column
does not exist, and emit useful diagnostics to let you know.

### `col_type_is( schema, table, column, schema, type, description )` ###
### `col_type_is( schema, table, column, schema, type )` ###
### `col_type_is( schema, table, column, type, description )` ###
### `col_type_is( schema, table, column, type )` ###
### `col_type_is( table, column, type, description )` ###
### `col_type_is( table, column, type )` ###

    SELECT col_type_is(
        'myschema',
        'sometable',
        'somecolumn',
        'numeric(10,2)',
        'Column myschema.sometable.somecolumn should be type text'
    );

This function tests that the specified column is of a particular type. If it
fails, it will emit diagnostics naming the actual type. The first argument is
the schema name, the second the table name, the third the column name, the
fourth the type's schem, the fifth the type, and the sixth is the test
description. If the table schema is omitted, the table and the type must be
visible in the search path. If the test description is omitted, it will be set
to "Column `:schema.:table.:column` should be type `:schema.:type`". Note that
this test will fail if the table or column in question does not exist.

The type argument should be formatted as it would be displayed in the view of
a table using the `\d` command in `psql`. For example, if you have a numeric
column with a precision of 8, you should specify "numeric(8,0)". If you
created a `varchar(64)` column, you should pass the type as "character
varying(64)".

If the test fails, it will output useful diagnostics. For example this test:

    SELECT col_type_is( 'pg_catalog', 'pg_type', 'typname', 'text' );

Will produce something like this:

    # Failed test 138: "Column pg_catalog.pg_type.typname should be type text"
    #         have: name
    #         want: text

It will even tell you if the test fails because a column doesn't exist or
actually has no default. But use `has_column()` to make sure the column exists
first, eh?

### `col_default_is( schema, table, column, default, description )` ###
### `col_default_is( table, column, default, description )` ###
### `col_default_is( table, column, default )` ###

    SELECT col_default_is(
        'myschema',
        'sometable',
        'somecolumn',
        'howdy'::text,
        'Column myschema.sometable.somecolumn should default to ''howdy'''
    );

Tests the default value of a column. If it fails, it will emit diagnostics
showing the actual default value. The first argument is the schema name, the
second the table name, the third the column name, the fourth the default
value, and the fifth is the test description. If the schema is omitted, the
table must be visible in the search path. If the test description is omitted,
it will be set to "Column `:table.:column` should default to `:default`". Note
that this test will fail if the table or column in question does not exist.

The default argument must have an unambiguous type in order for the call to
succeed. If you see an error such as 'ERROR: could not determine polymorphic
type because input has type "unknown"', it's because you forgot to cast the
expected value, probably a `NULL` (which, by the way, you can only properly
test for in PostgreSQL 8.3 and later), to its proper type. IOW, this will
fail:

    SELECT col_default_is( 'tab', age, NULL );

But this will not:

    SELECT col_default_is( 'tab', age, NULL::integer );

You can also test for functional defaults. Just specify the function call as a
string:

    SELECT col_default_is( 'user', 'created_at', 'now()' );

If the test fails, it will output useful diagnostics. For example, this test:

    SELECT col_default_is(
        'pg_catalog',
        'pg_type',
        'typname',
        'foo',
        'check typname'
    );

Will produce something like this:

    # Failed test 152: "check typname"
    #         have: NULL
    #         want: foo

And if the test fails because the table or column in question does not exist,
the diagnostics will tell you that, too. But you use `has_column()` and
`col_has_default()` to test those conditions before you call
`col_default_is()`, right? *Right???* Yeah, good, I thought so.

### `has_pk( schema, table, description )` ###
### `has_pk( table, description )` ###
### `has_pk( table )` ###

    SELECT has_pk(
        'myschema',
        'sometable',
        'Table myschema.sometable should have a primary key'
    );

Tests whether or not a table has a primary key. The first argument is the
schema name, the second the table name, the the third is the test description.
If the schema is omitted, the table must be visible in the search path. If the
test description is omitted, it will be set to "Table `:table` should have a
primary key". Note that this test will fail if the table in question does not
exist.

### `hasnt_pk( schema, table, description )` ###
### `hasnt_pk( table, description )` ###
### `hasnt_pk( table )` ###

    SELECT hasnt_pk(
        'myschema',
        'sometable',
        'Table myschema.sometable should not have a primary key'
    );

This function is the inverse of `has_pk()`. The test passes if the specified
primary key does *not* exist.

### `has_fk( schema, table, description )` ###
### `has_fk( table, description )` ###
### `has_fk( table )` ###

    SELECT has_fk(
        'myschema',
        'sometable',
        'Table myschema.sometable should have a foreign key constraint'
    );

Tests whether or not a table has a foreign key constraint. The first argument
is the schema name, the second the table name, the the third is the test
description. If the schema is omitted, the table must be visible in the search
path. If the test description is omitted, it will be set to "Table `:table`
should have a foreign key constraint". Note that this test will fail if the
table in question does not exist.

### `hasnt_fk( schema, table, description )` ###
### `hasnt_fk( table, description )` ###
### `hasnt_fk( table )` ###

    SELECT hasnt_fk(
        'myschema',
        'sometable',
        'Table myschema.sometable should not have a foreign key constraint'
    );

This function is the inverse of `has_fk()`. The test passes if the specified
foreign key does *not* exist.

### `col_is_pk( schema, table, column, description )` ###
### `col_is_pk( schema, table, column[], description )` ###
### `col_is_pk( table, column, description )` ###
### `col_is_pk( table, column[], description )` ###
### `col_is_pk( table, column )` ###
### `col_is_pk( table, column[] )` ###

    SELECT col_is_pk(
        'myschema',
        'sometable',
        'id',
        'Column myschema.sometable.id should be a primary key'
    );

    SELECT col_is_pk(
        'persons',
        ARRAY['given_name', 'surname'],
    );

Tests whether the specified column or columns in a table is/are the primary
key for that table. If it fails, it will emit diagnostics showing the actual
primary key columns, if any. The first argument is the schema name, the second
the table name, the third the column name or an array of column names, and the
fourth is the test description. If the schema is omitted, the table must be
visible in the search path. If the test description is omitted, it will be set
to "Column `:table(:column)` should be a primary key". Note that this test
will fail if the table or column in question does not exist.

If the test fails, it will output useful diagnostics. For example this test:

    SELECT col_is_pk( 'pg_type', 'id' );

Will produce something like this:

    # Failed test 178: "Column pg_type.id should be a primary key"
    #         have: {}
    #         want: {id}

### `col_isnt_pk( schema, table, column, description )` ###
### `col_isnt_pk( schema, table, column[], description )` ###
### `col_isnt_pk( table, column, description )` ###
### `col_isnt_pk( table, column[], description )` ###
### `col_isnt_pk( table, column )` ###
### `col_isnt_pk( table, column[] )` ###

    SELECT col_isnt_pk(
        'myschema',
        'sometable',
        'id',
        'Column myschema.sometable.id should not be a primary key'
    );

    SELECT col_isnt_pk(
        'persons',
        ARRAY['given_name', 'surname'],
    );

This function is the inverse of `col_is_pk()`. The test passes if the
specified column or columns are not a primary key.

### `col_is_fk( schema, table, column, description )` ###
### `col_is_fk( schema, table, column[], description )` ###
### `col_is_fk( table, column, description )` ###
### `col_is_fk( table, column[], description )` ###
### `col_is_fk( table, column )` ###
### `col_is_fk( table, column[] )` ###

    SELECT col_is_fk(
        'myschema',
        'sometable',
        'other_id',
        'Column myschema.sometable.other_id should be a foreign key'
    );

    SELECT col_is_fk(
        'contacts',
        ARRAY['given_name', 'surname'],
    );

Just like `col_is_fk()`, except that it test that the column or array of
columns are a primary key. The diagnostics on failure are a bit different,
too. Since the table might have more than one foreign key, the diagnostics
simply list all of the foreign key constraint columns, like so:

    #    Table widget has foreign key constraints on these columns:
    #        {thingy_id}
    #        {surname,given_name}

### `col_isnt_fk( schema, table, column, description )` ###
### `col_isnt_fk( schema, table, column[], description )` ###
### `col_isnt_fk( table, column, description )` ###
### `col_isnt_fk( table, column[], description )` ###
### `col_isnt_fk( table, column )` ###
### `col_isnt_fk( table, column[] )` ###

    SELECT col_isnt_fk(
        'myschema',
        'sometable',
        'other_id',
        'Column myschema.sometable.other_id should not be a foreign key'
    );

    SELECT col_isnt_fk(
        'contacts',
        ARRAY['given_name', 'surname'],
    );

This function is the inverse of `col_is_fk()`. The test passes if the
specified column or columns are not a foreign key.

### `fk_ok( fk_schema, fk_table, fk_column[], pk_schema, pk_table, pk_column[], description )` ###
### `fk_ok( fk_schema, fk_table, fk_column[], fk_schema, pk_table, pk_column[] )` ###
### `fk_ok( fk_table, fk_column[], pk_table, pk_column[], description )` ###
### `fk_ok( fk_table, fk_column[], pk_table, pk_column[] )` ###
### `fk_ok( fk_schema, fk_table, fk_column, pk_schema, pk_table, pk_column, description )` ###
### `fk_ok( fk_schema, fk_table, fk_column, pk_schema, pk_table, pk_column )` ###
### `fk_ok( fk_table, fk_column, pk_table, pk_column, description )` ###
### `fk_ok( fk_table, fk_column, pk_table, pk_column )` ###

    SELECT fk_ok(
        'myschema',
        'sometable',
        'big_id',
        'myschema',
        'bigtable',
        'id',
        'myschema.sometable(big_id) should reference myschema.bigtable(id)'
    );

    SELECT fk_ok(
        'contacts',
        ARRAY['person_given_name', 'person_surname'],
        'persons',
        ARRAY['given_name', 'surname'],
    );

This function combines `col_is_fk()` and `col_is_pk()` into a single test that
also happens to determine that there is in fact a foreign key relationship
between the foreign and primary key tables. To properly test your
relationships, this should be your main test function of choice.

The first three arguments are the schema, table, and column or array of
columns that constitute the foreign key constraint. The schema name is
optional, and the columns can be specified as a string for a single column or
an array of strings for multiple columns. The next three arguments are the
schema, table, and column or columns that constitute the corresponding primary
key. Again, the schema is optional and the columns may be a string or array of
strings (though of course it should have the same number of elements as the
foreign key column argument). The seventh argument is an optional description
If it's not included, it will be set to `:fk_schema.:fk_table(:fk_column)`
should reference `:pk_column.pk_table(:pk_column)`.

If the test fails, it will output useful diagnostics. For example this test:

    SELECT fk_ok( 'contacts', 'person_id', 'persons', 'id' );

Will produce something like this:

    # Failed test 178: "Column contacts(person_id) should reference persons(id)"
    #         have: contacts(person_id) REFERENCES persons(id)"
    #         want: contacts(person_nick) REFERENCES persons(nick)"

### `has_unique( schema, table, description )` ###
### `has_unique( table, description )` ###
### `has_unique( table )` ###

    SELECT has_unique(
        'myschema',
        'sometable',
        'Table myschema.sometable should have a unique constraint'
    );

Tests whether or not a table has a unique constraint. The first argument is
the schema name, the second the table name, the the third is the test
description. If the schema is omitted, the table must be visible in the search
path. If the test description is omitted, it will be set to "Table `:table`
should have a unique constraint". Note that this test will fail if the table
in question does not exist.

### `col_is_unique( schema, table, column, description )` ###
### `col_is_unique( schema, table, column[], description )` ###
### `col_is_unique( table, column, description )` ###
### `col_is_unique( table, column[], description )` ###
### `col_is_unique( table, column )` ###
### `col_is_unique( table, column[] )` ###

    SELECT col_is_unique(
        'myschema',
        'sometable',
        'other_id',
        'Column myschema.sometable.other_id should have a unique constraint'
    );

    SELECT col_is_unique(
        'contacts',
        ARRAY['given_name', 'surname'],
    );

Just like `col_is_pk()`, except that it test that the column or array of
columns have a unique constraint on them. In the event of failure, the
diagnostics will list the unique constraints that were actually found, if any:

    Failed test 40: "users.email should be unique"
            have: {username}
                  {first_name,last_name}
            want: {email}

### `has_check( schema, table, description )` ###
### `has_check( table, description )` ###
### `has_check( table )` ###

    SELECT has_check(
        'myschema',
        'sometable',
        'Table myschema.sometable should have a check constraint'
    );

    Failed test 41: "users.email should have a check constraint"
            have: {username}
            want: {email}

Tests whether or not a table has a check constraint. The first argument is the
schema name, the second the table name, the the third is the test description.
If the schema is omitted, the table must be visible in the search path. If the
test description is omitted, it will be set to "Table `:table` should have a
check constraint". Note that this test will fail if the table in question does
not exist.

In the event of failure, the diagnostics will list the columns on the table
that do have check constraints, if any:

### `col_has_check( schema, table, column, description )` ###
### `col_has_check( schema, table, column[], description )` ###
### `col_has_check( table, column, description )` ###
### `col_has_check( table, column[], description )` ###
### `col_has_check( table, column )` ###
### `col_has_check( table, column[] )` ###

    SELECT col_has_check(
        'myschema',
        'sometable',
        'other_id',
        'Column myschema.sometable.other_id should have a check constraint'
    );

    SELECT col_has_check(
        'contacts',
        ARRAY['given_name', 'surname'],
    );

Just like `col_is_pk()`, except that it test that the column or array of
columns have a check constraint on them.

### `index_is_unique( schema, table, index, description )` ###
### `index_is_unique( schema, table, index )` ###
### `index_is_unique( table, index )` ###
### `index_is_unique( index )` ###

    SELECT index_is_unique(
        'myschema',
        'sometable',
        'myindex',
        'Index "myindex" should be unique'
    );

    SELECT index_is_unique( 'sometable', 'myindex' );

Tests whether an index is unique.

### `index_is_primary( schema, table, index, description )` ###
### `index_is_primary( schema, table, index )` ###
### `index_is_primary( table, index )` ###
### `index_is_primary( index )` ###

    SELECT index_is_primary(
        'myschema',
        'sometable',
        'myindex',
        'Index "myindex" should be on a primary key'
    );

    SELECT index_is_primary( 'sometable', 'myindex' );

Tests whether an index is on a primary key.

### `is_clustered( schema, table, index, description )` ###
### `is_clustered( schema, table, index )` ###
### `is_clustered( table, index )` ###
### `is_clustered( index )` ###

    SELECT is_clustered(
        'myschema',
        'sometable',
        'myindex',
        'Table sometable should be clustered on "myindex"'
    );

    SELECT is_clustered( 'sometable', 'myindex' );

Tests whether a table is clustered on the given index. A table is clustered on
an index when the SQL command `CLUSTER TABLE INDEXNAME` has been executed.
Clustering reorganizes the table tuples so that they are stored on disk in the
order defined by the index.

### `index_is_type( schema, table, index, type, description )` ###
### `index_is_type( schema, table, index, type )` ###
### `index_is_type( table, index, type )` ###
### `index_is_type( index, type )` ###

    SELECT index_is_type(
        'myschema',
        'sometable',
        'myindex',
        'gist',
        'Index "myindex" should be a GIST index'
    );

    SELECT index_is_type( 'myindex', 'gin' );

Tests to ensure that an index is of a particular type. At the time of this
writing, the supported types are:

* btree
* hash
* gist
* gin

If the test fails, it will emit a diagnostic message with the actual index
type, like so:

    # Failed test 175: "Index idx_bar should be a hash index"
    #         have: btree
    #         want: hash

Feeling Funky
-------------

Perhaps more important than testing the database schema is testing your custom
functions. Especially if you write functions that provide the interface for
clients to interact with the database, making sure that they work will save
you time in the long run. So check out these assertions to maintain your
sanity.

### `can( schema, functions[], description )` ###
### `can( schema, functions[] )` ###
### `can( functions[], description )` ###
### `can( functions[] )` ###

    SELECT can( 'pg_catalog', ARRAY['upper', 'lower'] );

Checks to be sure that `:schema` has `:functions[]` defined. This is subtly
different from `functions_are()`. `functions_are()` fails if the functions
defined in `:schema` are not exactly the functions defined in `:functions[]`.
`can()`, on the other hand, just makes sure that `functions[]` exist.

If `:schema` is omitted, then `can()` will look for functions defined in
schemas defined in the search path. No matter how many functions are listed in
`:functions[]`, a single call to `can()` counts as one test. If you want
otherwise, call `can()` once for each function -- or better yet, use
`has_function()`.

If any of the functions are not defined, the test will fail and the
diagnostics will output a list of the functions that are missing, like so:

    # Failed test 52: "Schema pg_catalog can"
    #     pg_catalog.foo() missing
    #     pg_catalog.bar() missing

### `function_lang_is( schema, function, args[], language, description )` ###
### `function_lang_is( schema, function, args[], language )` ###
### `function_lang_is( schema, function, language, description )` ###
### `function_lang_is( schema, function, language )` ###
### `function_lang_is( function, args[], language, description )` ###
### `function_lang_is( function, args[], language )` ###
### `function_lang_is( function, language, description )` ###
### `function_lang_is( function, language )` ###

    SELECT function_lang_is(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'perl',
        'The myschema.foo() function should be written in Perl'
    );


    SELECT function_lang_is( 'do_something', 'sql' );
    SELECT function_lang_is( 'do_something', ARRAY['integer'], 'plpgsql' );
    SELECT function_lang_is( 'do_something', ARRAY['numeric'], 'plpgsql' );

Tests that a particular function is implemented in a particular procedural
language. The function name is required. If the `:schema` argument is omitted,
then the function must be visible in the search path. If the `:args[]`
argument is passed, then the function with that argument signature will be the
one tested; otherwise, a function with any signature will be checked (pass an
empty array to specify a function with an empty signature). If the
`:description` is omitted, a reasonable substitute will be created.

In the event of a failure, you'll useful diagnostics will tell you what went
wrong, for example:

    # Failed test 211: "Function mychema.eat(integer, text) should be written in perl"
    #         have: plpgsql
    #         want: perl

If the function does not exist, you'll be told that, too.

    # Failed test 212: "Function myschema.grab() should be written in sql
    #     Function myschema.grab() does not exist

But then you check with `has_function()` first, right?

### `function_returns( schema, function, args[], type, description )` ###
### `function_returns( schema, function, args[], type )` ###
### `function_returns( schema, function, type, description )` ###
### `function_returns( schema, function, type )` ###
### `function_returns( function, args[], type, description )` ###
### `function_returns( function, args[], type )` ###
### `function_returns( function, type, description )` ###
### `function_returns( function, type )` ###

    SELECT function_returns(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'integer',
        'The myschema.foo() function should return an integer'
    );

    SELECT function_returns( 'do_something', 'setof bool' );
    SELECT function_returns( 'do_something', ARRAY['integer'], 'boolean' );
    SELECT function_returns( 'do_something', ARRAY['numeric'], 'numeric' );

Tests that a particular function returns a particular data type. The `:args[]`
and `:type` arguments should be formatted as they would be displayed in the
view of a function using the `\df` command in `psql`. For example, use
"character varying" rather than "varchar", and "boolean" rather than "bool".
For set returning functions, the `:type` argument should start with "setof "
(yes, lowercase).

If the `:schema` argument is omitted, then the function must be visible in the
search path. If the `:args[]` argument is passed, then the function with that
argument signature will be the one tested; otherwise, a function with any
signature will be checked (pass an empty array to specify a function with an
empty signature). If the `:description` is omitted, a reasonable substitute
will be created.

In the event of a failure, you'll useful diagnostics will tell you what went
wrong, for example:

    # Failed test 283: "Function oww(integer, text) should return integer"
    #         have: bool
    #         want: integer

If the function does not exist, you'll be told that, too.

    # Failed test 284: "Function oui(integer, text) should return integer"
    #     Function oui(integer, text) does not exist

But then you check with `has_function()` first, right?

### `is_definer( schema, function, args[], description )` ###
### `is_definer( schema, function, args[] )` ###
### `is_definer( schema, function, description )` ###
### `is_definer( schema, function )` ###
### `is_definer( function, args[], description )` ###
### `is_definer( function, args[] )` ###
### `is_definer( function, description )` ###
### `is_definer( function )` ###

    SELECT is_definer(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'The myschema.foo() function should be security definer'
    );

    SELECT is_definer( 'do_something' );
    SELECT is_definer( 'do_something', ARRAY['integer'] );
    SELECT is_definer( 'do_something', ARRAY['numeric'] );

Tests that a function is a security definer (i.e., a "setuid" function). If
the `:schema` argument is omitted, then the function must be visible in the
search path. If the `:args[]` argument is passed, then the function with that
argument signature will be the one tested; otherwise, a function with any
signature will be checked (pass an empty array to specify a function with an
empty signature). If the `:description` is omitted, a reasonable substitute
will be created.

If the function does not exist, a handy diagnostic message will let you know:

    # Failed test 290: "Function nasty() should be security definer"
    #     Function nasty() does not exist

But then you check with `has_function()` first, right?

### `is_strict( schema, function, args[], description )` ###
### `is_strict( schema, function, args[] )` ###
### `is_strict( schema, function, description )` ###
### `is_strict( schema, function )` ###
### `is_strict( function, args[], description )` ###
### `is_strict( function, args[] )` ###
### `is_strict( function, description )` ###
### `is_strict( function )` ###

    SELECT is_strict(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'The myschema.foo() function should be strict
    );

    SELECT is_strict( 'do_something' );
    SELECT is_strict( 'do_something', ARRAY['integer'] );
    SELECT is_strict( 'do_something', ARRAY['numeric'] );

Tests that a function is a strict, meaning that the function returns null if
any argument is null. If the `:schema` argument is omitted, then the function
must be visible in the search path. If the `:args[]` argument is passed, then
the function with that argument signature will be the one tested; otherwise, a
function with any signature will be checked (pass an empty array to specify a
function with an empty signature). If the `:description` is omitted, a
reasonable substitute will be created.

If the function does not exist, a handy diagnostic message will let you know:

    # Failed test 290: "Function nasty() should be strict
    #     Function nasty() does not exist

But then you check with `has_function()` first, right?

### `is_aggregate( schema, function, args[], description )` ###
### `is_aggregate( schema, function, args[] )` ###
### `is_aggregate( schema, function, description )` ###
### `is_aggregate( schema, function )` ###
### `is_aggregate( function, args[], description )` ###
### `is_aggregate( function, args[] )` ###
### `is_aggregate( function, description )` ###
### `is_aggregate( function )` ###

    SELECT is_aggregate(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'The myschema.foo() function should be strict
    );

    SELECT is_aggregate( 'do_something' );
    SELECT is_aggregate( 'do_something', ARRAY['integer'] );
    SELECT is_aggregate( 'do_something', ARRAY['numeric'] );

Tests that a function is an aggregate function. If the `:schema` argument is
omitted, then the function must be visible in the search path. If the
`:args[]` argument is passed, then the function with that argument signature
will be the one tested; otherwise, a function with any signature will be
checked (pass an empty array to specify a function with an empty signature).
If the `:description` is omitted, a reasonable substitute will be created.

If the function does not exist, a handy diagnostic message will let you know:

    # Failed test 290: "Function nasty() should be strict
    #     Function nasty() does not exist

But then you check with `has_function()` first, right?

### `volatility_is( schema, function, args[], volatility, description )` ###
### `volatility_is( schema, function, args[], volatility )` ###
### `volatility_is( schema, function, volatility, description )` ###
### `volatility_is( schema, function, volatility )` ###
### `volatility_is( function, args[], volatility, description )` ###
### `volatility_is( function, args[], volatility )` ###
### `volatility_is( function, volatility, description )` ###
### `volatility_is( function, volatility )` ###

    SELECT volatility_is(
        'myschema',
        'foo',
        ARRAY['integer', 'text'],
        'stable',
        'The myschema.foo() function should be stable
    );


    SELECT volatility_is( 'do_something', 'immutable' );
    SELECT volatility_is( 'do_something', ARRAY['integer'], 'stable' );
    SELECT volatility_is( 'do_something', ARRAY['numeric'], 'volatile' );

Tests the volatility of a function. Supported volatilities are "volatile",
"stable", and "immutable". Consult the [`CREATE FUNCTION`
documentation](http://www.postgresql.org/docs/current/static/sql-createfunction.html)
for details. The function name is required. If the `:schema` argument is
omitted, then the function must be visible in the search path. If the
`:args[]` argument is passed, then the function with that argument signature
will be the one tested; otherwise, a function with any signature will be
checked (pass an empty array to specify a function with an empty signature).
If the `:description` is omitted, a reasonable substitute will be created.

In the event of a failure, you'll useful diagnostics will tell you what went
wrong, for example:

    # Failed test 211: "Function mychema.eat(integer, text) should be IMMUTABLE"
    #         have: VOLATILE
    #         want: IMMUTABLE

If the function does not exist, you'll be told that, too.

    # Failed test 212: "Function myschema.grab() should be IMMUTABLE
    #     Function myschema.grab() does not exist

But then you check with `has_function()` first, right?

### `trigger_is( schema, table, trigger, schema, function, description )` ###
### `trigger_is( schema, table, trigger, schema, function )` ###
### `trigger_is( table, trigger, function, description )` ###
### `trigger_is( table, trigger, function )` ###

    SELECT trigger_is(
        'myschema',
        'sometable',
        'sometrigger',
        'myschema',
        'somefunction',
        'Trigger "sometrigger" should call somefunction()'
    );

Tests that the specified trigger calls the named function. If not, it outputs
a useful diagnostic:

    # Failed test 31: "Trigger set_users_pass should call hash_password()"
    #         have: hash_pass
    #         want: hash_password

Database Deets
--------------

Tables and functions aren't the only objects in the database, as you well
know. These assertions close the gap by letting you test the attributes of
other database objects.

### `language_is_trusted( language, description )` ###
### `language_is_trusted( language )` ###

    SELECT language_is_trusted( 'plperl', 'PL/Perl should be trusted' );

Tests that the specified procedural language is trusted. See the [CREATE
LANGUAGE](http://www.postgresql.org/docs/current/static/sql-createlanguage.html
"CREATE LANGUAGE") documentation for details on trusted and untrusted
procedural languages. If the `:description` argument is not passed, a suitably
useful default will be created.

In the event that the language in question does not exist in the database,
`language_is_trusted()` will emit a diagnostic message to alert you to this
fact, like so:

    # Failed test 523: "Procedural language plomgwtf should be trusted"
    #     Procedural language plomgwtf does not exist

But you really ought to call `has_language()` first so that you never get that
far.

### `enum_has_labels( schema, enum, labels, description )` ###
### `enum_has_labels( schema, enum, labels )` ###
### `enum_has_labels( enum, labels, description )` ###
### `enum_has_labels( enum, labels )` ###

    SELECT enum_has_labels(
        'myschema',
        'someenum',
        ARRAY['foo', 'bar'],
        'Enum someenum should have labels foo, bar'
    );

This function tests that an enum consists of an expected list of labels. Enums
are supported in PostgreSQL 8.3 or higher. The first argument is a schema
name, the second an enum name, the third an array of enum labels, and the
fourth a description. If you omit the schema, the enum must be visible in the
search path. If you omit the test description, it will be set to "Enum `:enum`
should have labels (`:labels`)".

### `domain_type_is( schema, domain, schema, type, description )` ###
### `domain_type_is( schema, domain, schema, type )` ###
### `domain_type_is( schema, domain, type, description )` ###
### `domain_type_is( schema, domain, type )` ###
### `domain_type_is( domain, type, description )` ###
### `domain_type_is( domain, type )` ###

    SELECT domain_type_is(
        'public', 'us_postal_code',
        'public', 'text',
        'The us_postal_code domain should extend the text type'
    );

Tests the data type underlying a domain. The first two are arguments are the
schema and name of the domain. The second two are the schema and name of the
type that the domain should extend. The fifth argument is a description. If
there is no description, a reasonable default description will be created. The
schema arguments are also optional (though if there is no schema for the
domain then there cannot be one for the type). For the 3- and 4-argument
forms with schemas, cast the schemas to `NAME` to avoid ambiguities.

If the data type does not match the type that the domain extends, the test
will fail and output diagnostics like so:

    # Failed test 631: "Domain public.us_postal_code should extend type public.integer"
    #         have: public.text
    #         want: public.integer

If the domain in question does not actually exist, the test will fail with
diagnostics that tell you so:

    # Failed test 632: "Domain public.zip_code should extend type public.text"
    #    Domain public.zip_code does not exist

### `domain_type_isnt( schema, domain, schema, type, description )` ###
### `domain_type_isnt( schema, domain, schema, type )` ###
### `domain_type_isnt( schema, domain, type, description )` ###
### `domain_type_isnt( schema, domain, type )` ###
### `domain_type_isnt( domain, type, description )` ###
### `domain_type_isnt( domain, type )` ###

    SELECT domain_type_isnt(
        'public', 'us_postal_code',
        'public', 'integer',
        'The us_postal_code domain should not extend the integer type'
    );

The inverse of `domain_type_is()`, this function tests that a domain does
*not* extend a particular data type. For example, a US postal code domain
should probably extned the `text` type, not `integer`, since leading 0s are
valid and required. The arguments are the same as for `domain_type_is()`.

### `cast_context_is( source_type, target_type, context, description )` ###
### `cast_context_is( source_type, target_type, context )` ###

    SELECT cast_context_is( 'integer', 'bigint', 'implicit' );

Test that a cast from a source to a target data type has a particular context.
The data types should be passed as they are displayed by
`pg_catalog.format_type()`. For example, you would need to pass "character
varying", and not "VARCHAR".

The The supported contexts are "implicit", "assignment", and "explicit". You
can also just pass in "i", "a", or "e". Consult the PostgreSQL [`CREATE
CAST`](http://www.postgresql.org/docs/current/static/sql-createcast.html)
documentation for the differences between these contexts (hint: they
correspond to the default context, `AS IMPLICIT`, and `AS ASSIGNMENT`). If you
don't supply a test description, pgTAP will create a reasonable one for you.

Test failure will result in useful diagnostics, such as:

    # Failed test 124: "Cast ("integer" AS "bigint") context should be explicit"
    #         have: implicit
    #         want: explicit

If the cast doesn't exist, you'll be told that, too:

    # Failed test 199: "Cast ("integer" AS foo) context should be explicit"
    #     Cast ("integer" AS foo) does not exist

But you've already used `has_cast()` to make sure of that, right?

### `is_superuser( user, description )` ###
### `is_superuser( user )` ###

    SELECT is_superuser( 'theory', 'User "theory" should be a super user' );

Tests that a database user is a super user. If the description is omitted, it
will default to "User `:user` should be a super user". If the user does not
exist in the database, the diagnostics will say so.

### `isnt_superuser( user, description )` ###
### `isnt_superuser( user )` ###

    SELECT is_superuser(
        'dr_evil',
        'User "dr_evil" should not be a super user'
    );

The inverse of `is_superuser()`, this function tests that a database user is
*not* a super user. Note that if the named user does not exist in the
database, the test is still considered a failure, and the diagnostics will say
so.

### `is_member_of( group, users[], description )` ###
### `is_member_of( group, users[] )` ###
### `is_member_of( group, user, description )` ###
### `is_member_of( group, user )` ###

    SELECT is_member_of( 'sweeties', 'anna' 'Anna should be a sweetie' );
    SELECT is_member_of( 'meanies', ARRAY['dr_evil', 'dr_no' ] );

Checks whether a group contains a user or all of an array of users. If the
description is omitted, it will default to "Should have members of group
`:group`." On failure, `is_member_of()` will output diagnostics listing the
missing users, like so:

    # Failed test 370: "Should have members of group meanies"
    #     Users missing from the meanies group:
    #         theory
    #         agliodbs

If the group does not exist, the diagnostics will tell you that, instead. But
you use `has_group()` to make sure the group exists before you check its
members, don't you? Of course you do.

### `rule_is_instead( schema, table, rule, description )` ###
### `rule_is_instead( schema, table, rule )` ###
### `rule_is_instead( table, rule, description )` ###
### `rule_is_instead( table, rule )` ###

    SELECT rule_is_instead(
        'public',
        'users',
        'on_insert',
        'Rule "on_insert" should be on on relation public.users'
    );

Checks whether a rule on the specified relation is an `INSTEAD` rule. See the
[`CREATE RULE`
Documentation](http://www.postgresql.org/docs/current/static/sql-createrule.html)
for details. If the `:schema` argument is omitted, the relation must be
visible in the search path. If the `:description` argument is omitted, an
appropriate description will be created. In the event that the test fails
because the rule in question does not actually exist, you will see an
appropriate diagnostic such as:

    # Failed test 625: "Rule on_insert on relation public.users should be an INSTEAD rule"
    #     Rule on_insert does not exist

### `rule_is_on( schema, table, rule, event, description )` ###
### `rule_is_on( schema, table, rule, event )` ###
### `rule_is_on( table, rule, event, description )` ###
### `rule_is_on( table, rule, event )` ###

    SELECT rule_is_on(
        'public',
        'users',
        'on_insert',
        'INSERT',
        'Rule "on_insert" be on insert to on relation public.users'
    );

Tests the event for a rule, which may be one of `SELECT`, `INSERT`, `UPDATE`,
or `DELETE`. For the `:event` argument, you can specify the name of the event
in any case, or even with a single letter ("s", "i", "u", or "d"). If the
`:schema` argument is omitted, then the table must be visible in the search
path. If the `:description` is omitted, a reasonable default will be created.

If the test fails, you'll see useful diagnostics, such as:

    # Failed test 133: "Rule ins_me should be on INSERT to public.widgets"
    #         have: UPDATE
    #         want: INSERT

If the rule in question does not exist, you'll be told that, too:

    # Failed test 134: "Rule upd_me should be on UPDATE to public.widgets"
    #     Rule upd_me does not exist on public.widgets

But then you run `has_rule()` first, don't you?

No Test for the Wicked
======================

There is more to pgTAP. Oh *so* much more! You can output your own
[diagnostics](#Diagnostics). You can write [conditional
tests](#Conditional+Tests) based on the output of [utility
functions](#Utility+Functions). You can [batch up tests in
functions](#Tap+That+Batch). Read on to learn all about it.

Diagnostics
-----------

If you pick the right test function, you'll usually get a good idea of what
went wrong when it failed. But sometimes it doesn't work out that way. So here
we have ways for you to write your own diagnostic messages which are safer
than just `\echo` or `SELECT foo`.

### `diag( text )` ###
### `diag( anyelement )` ###
### `diag( variadic anyarray )` ###
### `diag( variadic text[] )` ###

Returns a diagnostic message which is guaranteed not to interfere with test
output. Handy for this sort of thing:

    -- Output a diagnostic message if the collation is not en_US.UTF-8.
    SELECT diag(
         E'These tests expect LC_COLLATE to be en_US.UTF-8,\n',
         'but yours is set to ', setting, E'.\n',
         'As a result, some tests may fail. YMMV.'
    )
      FROM pg_settings
     WHERE name = 'lc_collate'
       AND setting <> 'en_US.UTF-8';

Which would produce:

    # These tests expect LC_COLLATE to be en_US.UTF-8,
    # but yours is set to en_US.ISO8859-1.
    # As a result, some tests may fail. YMMV.

You can pass data of any type to `diag()` on PostgreSQL 8.3 and higher and it
will all be converted to text for the diagnostics. On PostgreSQL 8.4 and
higher, you can pass any number of arguments (as long as they are all the same
data type) and they will be concatenated together.

Conditional Tests
-----------------

Sometimes running a test under certain conditions will cause the test script
or function to die. A certain function or feature isn't implemented (such as
`pg_sleep()` prior to PostgreSQL 8.2), some resource isn't available (like a
procedural language), or a contrib module isn't available. In these cases it's
necessary to skip tests, or declare that they are supposed to fail but will
work in the future (a todo test).

### `skip( why, how_many )` ###
### `skip( how_many, why )` ###
### `skip( why )` ###
### `skip( how_many )` ###

Outputs SKIP test results. Use it in a conditional expression within a
`SELECT` statement to replace the output of a test that you otherwise would
have run.

    SELECT CASE WHEN pg_version_num() < 80100
        THEN skip('throws_ok() not supported before 8.1', 2 )
        ELSE collect_tap(
            throws_ok( 'SELECT 1/0', 22012, 'division by zero' ),
            throws_ok( 'INSERT INTO try (id) VALUES (1)', '23505' )
        ) END;

Note how use of the conditional `CASE` statement has been used to determine
whether or not to run a couple of tests. If they are to be run, they are run
through `collect_tap()`, so that we can run a few tests in the same query. If
we don't want to run them, we call `skip()` and tell it how many tests we're
skipping.

If you don't specify how many tests to skip, `skip()` will assume that you're
skipping only one. This is useful for the simple case, of course:

    SELECT CASE current_schema()
        WHEN 'public' THEN is( :this, :that )
        ELSE skip( 'Tests not running in the "public" schema' )
        END;

But you can also use it in a `SELECT` statement that would otherwise return
multiple rows:

    SELECT CASE current_schema()
        WHEN 'public' THEN is( nspname, 'public' )
        ELSE skip( 'Cannot see the public schema' )
        END
      FROM pg_namespace;

This will cause it to skip the same number of rows as would have been tested
had the `WHEN` condition been true.

### `todo( why, how_many )` ###
### `todo( how_many, why )` ###
### `todo( how_many )` ###
### `todo( why )` ###

Declares a series of tests that you expect to fail and why. Perhaps it's
because you haven't fixed a bug or haven't finished a new feature:

    SELECT todo('URIGeller not finished', 2);

    \set card '\'Eight of clubs\''
    SELECT is( URIGeller.yourCard(), :card, 'Is THIS your card?' );
    SELECT is( URIGeller.bendSpoon(), 'bent', 'Spoon bending, how original' );

With `todo()`, `:how_many` specifies how many tests are expected to fail. If
`:how_many` is omitted, it defaults to 1. pgTAP will run the tests normally,
but print out special flags indicating they are "todo" tests. The test harness
will interpret these failures as ok. Should any todo test pass, the harness
will report it as an unexpected success. You then know that the thing you had
todo is done and can remove the call to `todo()`.

The nice part about todo tests, as opposed to simply commenting out a block of
tests, is that they're like a programmatic todo list. You know how much work
is left to be done, you're aware of what bugs there are, and you'll know
immediately when they're fixed.

### `todo_start( why )` ###
### `todo_start( )` ###

This function allows you declare all subsequent tests as TODO tests, up until
the `todo_end()` function is called.

The `todo()` syntax is generally pretty good about figuring out whether or not
we're in a TODO test. However, often we find it difficult to specify the
*number* of tests that are TODO tests. Thus, you can instead use
`todo_start()` and `todo_end()` to more easily define the scope of your TODO
tests.

Note that you can nest TODO tests, too:

    SELECT todo_start('working on this');
    -- lots of code
    SELECT todo_start('working on that');
    -- more code
    SELECT todo_end();
    SELECT todo_end();

This is generally not recommended, but large testing systems often have weird
internal needs.

The `todo_start()` and `todo_end()` function should also work with the
`todo()` function, although it's not guaranteed and its use is also
discouraged:


    SELECT todo_start('working on this');
    -- lots of code
    SELECT todo('working on that', 2);
    -- Two tests for which the above line applies
    -- Followed by more tests scoped till the following line.
    SELECT todo_end();

We recommend that you pick one style or another of TODO to be on the safe
side.

### `todo_end()` ###

Stops running tests as TODO tests. This function is fatal if called without a
preceding `todo_start()` method call.

### `in_todo()` ###

Returns true if the test is currently inside a TODO block.

Utility Functions
-----------------

Along with the usual array of testing, planning, and diagnostic functions,
pTAP provides a few extra functions to make the work of testing more pleasant.

### `pgtap_version()` ###

    SELECT pgtap_version();

Returns the version of pgTAP installed in the server. The value is `NUMERIC`,
and thus suitable for comparing to a decimal value:

    SELECT CASE WHEN pgtap_version() < 0.17
        THEN skip('No sequence assertions before pgTAP 0.17')
        ELSE has_sequence('my_big_seq')
        END;

### `pg_version()` ###

    SELECT pg_version();

Returns the server version number against which pgTAP was compiled. This is
the stringified version number displayed in the first part of the core
`version()` function and stored in the "server_version" setting:

    try=% select current_setting( 'server_version'), pg_version();
     current_setting | pg_version
    -----------------+------------
     8.3.4           | 8.3.4
    (1 row)

### `pg_version_num()` ###

    SELECT pg_version_num();

Returns an integer representation of the server version number against which
pgTAP was compiled. This function is useful for determining whether or not
certain tests should be run or skipped (using `skip()`) depending on the
version of PostgreSQL. For example:

    SELECT CASE WHEN pg_version_num() < 80100
        THEN skip('throws_ok() not supported before 8.1' )
        ELSE throws_ok( 'SELECT 1/0', 22012, 'division by zero' )
        END;

The revision level is in the tens position, the minor version in the thousands
position, and the major version in the ten thousands position and above
(assuming PostgreSQL 10 is ever released, it will be in the hundred thousands
position). This value is the same as the `server_version_num` setting
available in PostgreSQL 8.2 and higher, but supported by this function back to
PostgreSQL 8.0:

    try=% select current_setting( 'server_version_num'), pg_version_num();
     current_setting | pg_version_num
    -----------------+----------------
     80304           |          80304

### `os_name()` ###

    SELECT os_name();

Returns a string representing the name of the operating system on which pgTAP
was compiled. This can be useful for determining whether or not to skip tests
on certain operating systems.

This is usually the same a the output of `uname`, but converted to lower case.
There are some semantics in the pgTAP build process to detect other operating
systems, though assistance in improving such detection would be greatly
appreciated.

**NOTE:** The values returned by this function may change in the future,
depending on how good the pgTAP build process gets at detecting a OS.

### `collect_tap(tap[])` ###

    SELECT collect_tap(
        ok(true, 'This should pass'),
        ok(false, 'This should fail)
    );

Collects the results of one or more pgTAP tests and returns them all. Useful
when used in combination with `skip()`:

      SELECT CASE os_name() WHEN 'darwin' THEN
          collect_tap(
              cmp_ok( 'Bjørn'::text, '>', 'Bjorn', 'ø > o' ),
              cmp_ok( 'Pınar'::text, '>', 'Pinar', 'ı > i' ),
              cmp_ok( 'José'::text,  '>', 'Jose',  'é > e' ),
              cmp_ok( 'Täp'::text,   '>', 'Tap',   'ä > a' )
          )
      ELSE
           skip('Collation-specific test', 4)
      END;

On PostgreSQL 8.4 and higher, it can take any number of arguments. Lower than
8.4 requires the explicit use of an array:

    SELECT collect_tap(ARRAY[
        ok(true, 'This should pass'),
        ok(false, 'This should fail)
    ]);

### `display_type( schema, type_oid, typemod )` ###
### `display_type( type_oid, typemod )` ###

    SELECT display_type('public', 'varchar'::regtype, NULL );
    SELECT display_type('numeric'::regtype, 196612 );

Like `pg_catalog.format_type()`, except that the returned value is not
prepended with the schema name unless it is passed as the first argument. Used
internally by pgTAP to compare type names, but may be more generally useful.

### `display_oper( oper_name, oper_oid )` ###

    SELECT display_type(oprname, oid ) FROM pg_operator;

Similar to casting an operator OID to regoperator, only the schema is not
included in the display. Used internally by pgTAP to compare operators, but
may be more generally useful.

### `pg_typeof(any)` ###

    SELECT pg_typeof(:value);

Returns a `regtype` identifying the type of value passed to the function. This
function is used internally by `cmp_ok()` to properly construct types when
executing the comparison, but might be generally useful.

    try=% select pg_typeof(12), pg_typeof(100.2);
     pg_typeof | pg_typeof
    -----------+-----------
     integer   | numeric

*Note:* pgTAP does not build `pg_typeof()` on PostgreSQL 8.4 or higher,
because it's in core in 8.4. You only need to worry about this if you depend
on the function being in particular schema. It will always be in `pg_catalog`
in 8.4 and higher.

### `findfuncs( schema, pattern )` ###
### `findfuncs( pattern )` ###

    SELECT findfuncs('myschema', '^test' );

This function searches the named schema or, if no schema is passed, the search
patch, for all functions that match the regular expression pattern. The
functions it finds are returned as an array of text values, with each value
consisting of the schema name, a dot, and the function name. For example:

    SELECT findfuncs('tests', '^test);
                findfuncs
    -----------------------------------
     {tests.test_foo,tests."test bar"}
    (1 row)

Tap that Batch
--------------

Sometimes it can be useful to batch a lot of TAP tests into a function. The
simplest way to do so is to define a function that `RETURNS SETOF TEXT` and
then simply call `RETURN NEXT` for each TAP test. Here's a simple example:

    CREATE OR REPLACE FUNCTION my_tests(
    ) RETURNS SETOF TEXT AS $$
    BEGIN
        RETURN NEXT pass( 'plpgsql simple' );
        RETURN NEXT pass( 'plpgsql simple 2' );
    END;
    $$ LANGUAGE plpgsql;

Then you can just call the function to run all of your TAP tests at once:

    SELECT plan(2);
    SELECT * FROM my_tests();
    SELECT * FROM finish();

### `do_tap( schema, pattern )` ###
### `do_tap( schema )` ###
### `do_tap( pattern )` ###
### `do_tap()` ###

    SELECT plan(32);
    SELECT * FROM do_tap('testschema'::name);
    SELECT * FROM finish();

If you like you can create a whole slew of these batched tap functions, and
then use the `do_tap()` function to run them all at once. If passed no
arguments, it will attempt to find all visible functions that start with
"test". If passed a schema name, it will look for and run test functions only
in that schema (be sure to cast the schema to `name` if it is the only
argument). If passed a regular expression pattern, it will look for function
names that match that pattern in the search path. If passed both, it will of
course only search for test functions that match the function in the named
schema.

This can be very useful if you prefer to keep all of your TAP tests in
functions defined in the database. Simply call `plan()`, use `do_tap()` to
execute all of your tests, and then call `finish()`.

As a bonus, if `client_min_messages` is set to "warning", "error", "fatal", or
"panic", the name of each function will be emitted as a diagnostic message
before it is called. For example, if `do_tap()` found and executed two TAP
testing functions an `client_min_messages` is set to "warning", output will
look something like this:

    # public.test_this()
    ok 1 - simple pass
    ok 2 - another simple pass
    # public.test_that()
    ok 3 - that simple
    ok 4 - that simple 2

Which will make it much easier to tell what functions need to be examined for
failing tests.

### `runtests( schema, match )` ###
### `runtests( schema )` ###
### `runtests( match )` ###
### `runtests( )` ###

    SELECT * FROM runtests( 'testschema', '^test' );

If you'd like pgTAP to plan, run all of your tests functions, and finish all
in one fell swoop, use `runtests()`. This most closely emulates the xUnit
testing environment, similar to the functionality of
[PGUnit](http://en.dklab.ru/lib/dklab_pgunit/) and
[Epic](http://www.epictest.org/). It requires PostgreSQL 8.1 or higher.

As with `do_tap()`, you can pass in a schema argument and/or a pattern that
the names of the tests functions can match. If you pass in only the schema
argument, be sure to cast it to `name` to identify it as a schema name rather
than a pattern:

    SELECT * FROM runtests('testschema'::name);

Unlike `do_tap()`, `runtests()` fully supports startup, shutdown, setup, and
teardown functions, as well as transactional rollbacks between tests. It also
outputs the test plan and fishes the tests, so you don't have to call `plan()`
or `finish()` yourself.

The fixture functions run by `runtests()` are as follows:

* `^startup` - Functions whose names start with "startup" are run in
  alphabetical order before any test functions are run.
* `^setup` - Functions whose names start with "setup" are run in alphabetical
  order before each test function is run.
* `^teardown` - Functions whose names start with "teardown" are run in
  alphabetical order after each test function is run. They will not be run,
  however, after a test that has died.
* `^shutdown` - Functions whose names start with "shutdown" are run in
  alphabetical order after all test functions have been run.

Note that all tests executed by `runtests()` are run within a single
transaction, and each test is run in a subtransaction that also includes
execution all the setup and teardown functions. All transactions are rolled
back after each test function, and at the end of testing, leaving your
database in largely the same condition as it was in when you started it (the
one exception I'm aware of being sequences, which are not rolled back to the
value used at the beginning of a rolled-back transaction).

Compose Yourself
================

So, you've been using pgTAP for a while, and now you want to write your own
test functions. Go ahead; I don't mind. In fact, I encourage it. How? Why,
by providing a function you can use to test your tests, of course!

But first, a brief primer on writing your own test functions. There isn't much
to it, really. Just write your function to do whatever comparison you want. As
long as you have a boolean value indicating whether or not the test passed,
you're golden. Just then use `ok()` to ensure that everything is tracked
appropriately by a test script.

For example, say that you wanted to create a function to ensure that two text
values always compare case-insensitively. Sure you could do this with `is()`
and the `LOWER()` function, but if you're doing this all the time, you might
want to simplify things. Here's how to go about it:

    CREATE OR REPLACE FUNCTION lc_is (text, text, text)
    RETURNS TEXT AS $$
    DECLARE
        result BOOLEAN;
    BEGIN
        result := LOWER($1) = LOWER($2);
        RETURN ok( result, $3 ) || CASE WHEN result THEN '' ELSE E'\n' || diag(
               '    Have: ' || $1 ||
            E'\n    Want: ' || $2;
    ) END;
    END;
    $$ LANGUAGE plpgsql;

Yep, that's it. The key is to always use pgTAP's `ok()` function to guarantee
that the output is properly formatted, uses the next number in the sequence,
and the results are properly recorded in the database for summarization at
the end of the test script. You can also provide diagnostics as appropriate;
just append them to the output of `ok()` as we've done here.

Of course, you don't have to directly use `ok()`; you can also use another
pgTAP function that ultimately calls `ok()`. IOW, while the above example
is instructive, this version is easier on the eyes:

    CREATE OR REPLACE FUNCTION lc_is ( TEXT, TEXT, TEXT )
    RETURNS TEXT AS $$
         SELECT is( LOWER($1), LOWER($2), $3);
    $$ LANGUAGE sql;

But either way, let pgTAP handle recording the test results and formatting the
output.

Testing Test Functions
----------------------

Now you've written your test function. So how do you test it? Why, with this
handy-dandy test function!

### `check_test( test_output, is_ok, name, want_description, want_diag, match_diag )` ###
### `check_test( test_output, is_ok, name, want_description, want_diag )` ###
### `check_test( test_output, is_ok, name, want_description )` ###
### `check_test( test_output, is_ok, name )` ###
### `check_test( test_output, is_ok )` ###

    SELECT * FROM check_test(
        lc_eq('This', 'THAT', 'not eq'),
        false,
        'lc_eq fail',
        'not eq',
        E'    Want: this\n    Have: that'
    );

    SELECT * FROM check_test(
        lc_eq('This', 'THIS', 'eq'),
        true
    );

This function runs anywhere between one and three tests against a test
function. For the impatient, the arguments are:

* `:test_output` - The output from your test. Usually it's just returned by a
  call to the test function itself. Required.
* `:is_ok` - Boolean indicating whether or not the test is expected to pass.
  Required.
* `:name` - A brief name for your test, to make it easier to find failures in
  your test script. Optional.
* `:want_description` - Expected test description to be output by the test.
  Optional. Use an empty string to test that no description is output.
* `:want_diag` - Expected diagnostic message output during the execution of
  a test. Must always follow whatever is output by the call to `ok()`.
  Optional. Use an empty string to test that no description is output.
* `:match_diag` - Use `matches()` to compare the diagnostics rather than
  `:is()`. Useful for those situations where you're not sure what will be in
  the output, but you can match it with a regular expression.

Now, on with the detailed documentation. At its simplest, you just pass in the
output of your test function (and it must be one and **only one** test
function's output, or you'll screw up the count, so don't do that!) and a
boolean value indicating whether or not you expect the test to have passed.
That looks something like the second example above.

All other arguments are optional, but I recommend that you *always* include a
short test name to make it easier to track down failures in your test script.
`check_test()` uses this name to construct descriptions of all of the tests it
runs. For example, without a short name, the above example will yield output
like so:

    not ok 14 - Test should pass

Yeah, but which test? So give it a very succinct name and you'll know what
test. If you have a lot of these, it won't be much help. So give each call
to `check_test()` a name:

    SELECT * FROM check_test(
        lc_eq('This', 'THIS', 'eq'),
        true,
        'Simple lc_eq test',
    );

Then you'll get output more like this:

    not ok 14 - Simple lc_test should pass

Which will make it much easier to find the failing test in your test script.

The optional fourth argument is the description you expect to be output. This
is especially important if your test function generates a description when
none is passed to it. You want to make sure that your function generates the
test description you think it should! This will cause a second test to be run
on your test function. So for something like this:

    SELECT * FROM check_test(
        lc_eq( ''this'', ''THIS'' ),
        true,
        'lc_eq() test',
        'this is THIS'
    );

The output then would look something like this, assuming that the `lc_eq()`
function generated the proper description (the above example does not):

    ok 42 - lc_eq() test should pass
    ok 43 - lc_eq() test should have the proper description

See how there are two tests run for a single call to `check_test()`? Be sure
to adjust your plan accordingly. Also note how the test name was used in the
descriptions for both tests.

If the test had failed, it would output a nice diagnostics. Internally it just
uses `is()` to compare the strings:

    # Failed test 43:  "lc_eq() test should have the proper description"
    #         have: 'this is this'
    #         want: 'this is THIS'

The fifth argument, `:want_diag`, which is also optional, compares the
diagnostics generated during the test to an expected string. Such diagnostics
**must** follow whatever is output by the call to `ok()` in your test. Your
test function should not call `diag()` until after it calls `ok()` or things
will get truly funky.

Assuming you've followed that rule in your `lc_eq()` test function, see what
happens when a `lc_eq()` fails. Write your test to test the diagnostics like
so:

    SELECT * FROM check_test(
        lc_eq( ''this'', ''THat'' ),
        false,
        'lc_eq() failing test',
        'this is THat',
        E'    Want: this\n    Have: THat
    );

This of course triggers a third test to run. The output will look like so:

    ok 44 - lc_eq() failing test should fail
    ok 45 - lc_eq() failing test should have the proper description
    ok 46 - lc_eq() failing test should have the proper diagnostics

And of course, it the diagnostic test fails, it will output diagnostics just
like a description failure would, something like this:

    # Failed test 46:  "lc_eq() failing test should have the proper diagnostics"
    #         have:     Have: this
    #     Want: that
    #         want:     Have: this
    #     Want: THat

If you pass in the optional sixth argument, `:match_diag`, the `:want_diag`
argument will be compared to the actual diagnostic output using `matches()`
instead of `is()`. This allows you to use a regular expression in the
`:want_diag` argument to match the output, for those situations where some
part of the output might vary, such as time-based diagnostics.

I realize that all of this can be a bit confusing, given the various haves and
wants, but it gets the job done. Of course, if your diagnostics use something
other than indented "have" and "want", such failures will be easier to read.
But either way, *do* test your diagnostics!

Compatibility
=============

Here are some notes on how pgTAP is built for particular versions of
PostgreSQL. This helps you to understand any side-effects. If you'd rather not
have these changes in your schema, build `pgTAP` with a schema just for it,
instead:

    make TAPSCHEMA=tap

To see the specifics for each version of PostgreSQL, consult the files in the
`compat/` directory in the pgTAP distribution.

8.4 and Up
----------

No changes. Everything should just work.

8.3 and Down
------------
* A patch is applied to modify `results_eq()` and `row_eq()` to cast records
  to text before comparing them. This means that things will mainly be
  correct, but it also means that two queries with incompatible types that
  convert to the same text string may be considered incorrectly equivalent.
* A C function, `pg_typeof()`, is built and installed in a DSO. This is for
  compatibility with the same function that ships in 8.4 core, and is required
  for `cmp_ok()` and `isa_ok()` to work.
* The variadic forms of `diag()` and `collect_tap()` are not available.
  You can pass an array of TAP to `collect_tap()`, however.

8.2 and Down
------------
* A patch is applied that removes the `enum_has_labels()`, the
  `diag(anyelement)` function and `col_has_default()` cannot be used to test
  for columns specified with `DEFAULT NULL` (even though that's the implied
  default default). The `has_enums()` function won't work. Also, a number of
  assignments casts are added to increase compatibility. The casts are:

  + `boolean` to `text`
  + `text[]` to `text`
  + `name[]` to `text`
  + `regtype` to `text`
* Two operators, `=` and `<>`, are added to compare `name[]` values.

8.0
---
* A patch is applied that changes how some of the test functions are written.
* A few casts are added for compatibility:
  + `oidvector` to `regtypep[]`.
  + `int2vector` to `integer[]`.
* The following functions do not work under 8.0. Don't even use them there:
* `throws_ok()`
* `throws_like()`
* `throws_ilike()`
* `throws_matching()`
* `throws_imatching()`
* `lives_ok()`
* `runtests()`
* `has_role()`
* `hasnt_role()`
* `roles_are()`
* `row_eq()`

To Do
=====
* Add `isnt_empty()` to complement `is_empty()`.
* Add variants of `set_eq()`, `bag_eq()`, and `results_eq()` that take an
  array of records as the second argument.
* Add `schema, table, colname` variations of the table-checking functions
  (`table_has_column()`, `col_type_is()`, etc.). That is, allow the
  description to be optional if the schema is included in the function call.
* Add functions to test for object ownership.
  + `db_owner_is()`
  + `table_owner_is()`
  + `view_owner_is()`
  + `sequence_owner_is()`
  + `function_owner_is()`
  + etc.
* Add some sort of tests for permisions. Something like:
  `table_privs_are(:table, :user, :privs[])`, and would have variations for
  database, sequence, function, language, schema, and tablespace.
* Have `has_function()` manage OUT, INOUT, and VARIADIC arguments.
* Useful schema testing functions to consider adding:
  + `sequence_has_range()`
  + `sequence_increments_by()`
  + `sequence_starts_at()`
  + `sequence_cycles()`
* Useful result testing function to consider adding (but might require C
  code): `rowtype_is()`
* Split test functions into separate files (and maybe distributions?):
  + One with scalar comparison functions: `ok()`, `is()`, `like()`, etc.
  + One with relation comparison functions: `set_eq()`, `results_eq()`, etc.
  + One with schema testing functions: `has_table()`, `tables_are()`, etc.
* Add useful negation function tests:
  + `isnt_definer()`
  + `isnt_strict()`
  + `isnt_aggregate()`
* Modify function testing assertions so that functions can be specified with
  full signatures, so that a polymorphic functions can be independently tested
  for language, volatility, etc.

Metadata
========

Public Repository
-----------------

The source code for pgTAP is available on
[GitHub](http://github.com/theory/pgtap/tree/). Please feel free to fork and
contribute!

Mail List
---------

Join the pgTAP community by subscribing to the [pgtap-users mail
list](http://pgfoundry.org/mailman/listinfo/pgtap-users). All questions,
comments, suggestions, and bug reports are welcomed there.

Author
------

[David E. Wheeler](http://justatheory.com/)

Credits
-------

* Michael Schwern and chromatic for Test::More.
* Adrian Howard for Test::Exception.

Copyright and License
---------------------

Copyright (c) 2008-2011 Kineticode, Inc. Some rights reserved.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement is
hereby granted, provided that the above copyright notice and this paragraph
and the following two paragraphs appear in all copies.

IN NO EVENT SHALL KINETICODE BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING
OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF KINETICODE HAS
BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

KINETICODE SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND
KINETICODE HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
ENHANCEMENTS, OR MODIFICATIONS.
