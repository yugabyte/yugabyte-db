\unset ECHO
\i test/setup.sql

SELECT plan(54);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2),
    "myInt" NUMERIC(8)
);

CREATE TYPE public.sometype AS (
    id    INT,
    name  TEXT
);

CREATE SEQUENCE public.someseq;

CREATE SCHEMA someschema;

RESET client_min_messages;

/****************************************************************************/
SELECT * FROM check_test(
    db_owner_is(current_database(), current_user, 'mumble'),
	true,
    'db_owner_is(db, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    db_owner_is(current_database(), current_user),
	true,
    'db_owner_is(db, user)',
    'Database ' || quote_ident(current_database()) || ' should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    db_owner_is('__not__' || current_database(), current_user, 'mumble'),
	false,
    'db_owner_is(non-db, user)',
    'mumble',
    '    Database __not__' || current_database() || ' does not exist'
);

SELECT * FROM check_test(
    db_owner_is(current_database(), '__not__' || current_user, 'mumble'),
	false,
    'db_owner_is(db, non-user)',
    'mumble',
    '        have: ' || current_user || '
        want: __not__' || current_user
);

/****************************************************************************/
-- Test rel_owner_is() with a table.
SELECT * FROM check_test(
    rel_owner_is('public', 'sometab', current_user, 'mumble'),
	true,
    'rel_owner_is(sch, tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    rel_owner_is('public', 'sometab', current_user),
	true,
    'rel_owner_is(sch, tab, user)',
    'Relation public.sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    rel_owner_is('__not__public', 'sometab', current_user, 'mumble'),
	false,
    'rel_owner_is(non-sch, tab, user)',
    'mumble',
    '    Relation __not__public.sometab does not exist'
);

SELECT * FROM check_test(
    rel_owner_is('public', '__not__sometab', current_user, 'mumble'),
	false,
    'rel_owner_is(sch, non-tab, user)',
    'mumble',
    '    Relation public.__not__sometab does not exist'
);

SELECT * FROM check_test(
    rel_owner_is('sometab', current_user, 'mumble'),
	true,
    'rel_owner_is(tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    rel_owner_is('sometab', current_user),
	true,
    'rel_owner_is(tab, user)',
    'Relation sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    rel_owner_is('__not__sometab', current_user, 'mumble'),
	false,
    'rel_owner_is(non-tab, user)',
    'mumble',
    '    Relation __not__sometab does not exist'
);

/****************************************************************************/
-- Test rel_owner_is() with a schema.
SELECT * FROM check_test(
    rel_owner_is('public', 'someseq', current_user, 'mumble'),
	true,
    'rel_owner_is(sch, seq, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    rel_owner_is('public', 'someseq', current_user),
	true,
    'rel_owner_is(sch, seq, user)',
    'Relation public.someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    rel_owner_is('__not__public', 'someseq', current_user, 'mumble'),
	false,
    'rel_owner_is(non-sch, seq, user)',
    'mumble',
    '    Relation __not__public.someseq does not exist'
);

SELECT * FROM check_test(
    rel_owner_is('public', '__not__someseq', current_user, 'mumble'),
	false,
    'rel_owner_is(sch, non-seq, user)',
    'mumble',
    '    Relation public.__not__someseq does not exist'
);

SELECT * FROM check_test(
    rel_owner_is('someseq', current_user, 'mumble'),
	true,
    'rel_owner_is(seq, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    rel_owner_is('someseq', current_user),
	true,
    'rel_owner_is(seq, user)',
    'Relation someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    rel_owner_is('__not__someseq', current_user, 'mumble'),
	false,
    'rel_owner_is(non-seq, user)',
    'mumble',
    '    Relation __not__someseq does not exist'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;

