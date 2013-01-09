\unset ECHO
\i test/setup.sql

SELECT plan(135);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2),
    "myInt" NUMERIC(8)
);

CREATE VIEW public.someview AS SELECT * FROM public.sometab;

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
-- Test table_owner_is().
SELECT * FROM check_test(
    table_owner_is('public', 'sometab', current_user, 'mumble'),
	true,
    'table_owner_is(sch, tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    table_owner_is('public', 'sometab', current_user),
	true,
    'table_owner_is(sch, tab, user)',
    'Table public.sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    table_owner_is('__not__public', 'sometab', current_user, 'mumble'),
	false,
    'table_owner_is(non-sch, tab, user)',
    'mumble',
    '    Table __not__public.sometab does not exist'
);

SELECT * FROM check_test(
    table_owner_is('public', '__not__sometab', current_user, 'mumble'),
	false,
    'table_owner_is(sch, non-tab, user)',
    'mumble',
    '    Table public.__not__sometab does not exist'
);

SELECT * FROM check_test(
    table_owner_is('sometab', current_user, 'mumble'),
	true,
    'table_owner_is(tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    table_owner_is('sometab', current_user),
	true,
    'table_owner_is(tab, user)',
    'Table sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    table_owner_is('__not__sometab', current_user, 'mumble'),
	false,
    'table_owner_is(non-tab, user)',
    'mumble',
    '    Table __not__sometab does not exist'
);

-- It should ignore the sequence.
SELECT * FROM check_test(
    table_owner_is('public', 'someseq', current_user, 'mumble'),
	false,
    'table_owner_is(sch, seq, user, desc)',
    'mumble',
    '    Table public.someseq does not exist'
);

SELECT * FROM check_test(
    table_owner_is('someseq', current_user, 'mumble'),
	false,
    'table_owner_is(seq, user, desc)',
    'mumble',
    '    Table someseq does not exist'
);

/****************************************************************************/
-- Test view_owner_is().
SELECT * FROM check_test(
    view_owner_is('public', 'someview', current_user, 'mumble'),
	true,
    'view_owner_is(sch, view, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    view_owner_is('public', 'someview', current_user),
	true,
    'view_owner_is(sch, view, user)',
    'View public.someview should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    view_owner_is('__not__public', 'someview', current_user, 'mumble'),
	false,
    'view_owner_is(non-sch, view, user)',
    'mumble',
    '    View __not__public.someview does not exist'
);

SELECT * FROM check_test(
    view_owner_is('public', '__not__someview', current_user, 'mumble'),
	false,
    'view_owner_is(sch, non-view, user)',
    'mumble',
    '    View public.__not__someview does not exist'
);

SELECT * FROM check_test(
    view_owner_is('someview', current_user, 'mumble'),
	true,
    'view_owner_is(view, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    view_owner_is('someview', current_user),
	true,
    'view_owner_is(view, user)',
    'View someview should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    view_owner_is('__not__someview', current_user, 'mumble'),
	false,
    'view_owner_is(non-view, user)',
    'mumble',
    '    View __not__someview does not exist'
);

-- It should ignore the sequence.
SELECT * FROM check_test(
    view_owner_is('public', 'someseq', current_user, 'mumble'),
	false,
    'view_owner_is(sch, seq, user, desc)',
    'mumble',
    '    View public.someseq does not exist'
);

SELECT * FROM check_test(
    view_owner_is('someseq', current_user, 'mumble'),
	false,
    'view_owner_is(seq, user, desc)',
    'mumble',
    '    View someseq does not exist'
);

/****************************************************************************/
-- Test sequence_owner_is().
SELECT * FROM check_test(
    sequence_owner_is('public', 'someseq', current_user, 'mumble'),
	true,
    'sequence_owner_is(sch, sequence, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    sequence_owner_is('public', 'someseq', current_user),
	true,
    'sequence_owner_is(sch, sequence, user)',
    'Sequence public.someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    sequence_owner_is('__not__public', 'someseq', current_user, 'mumble'),
	false,
    'sequence_owner_is(non-sch, sequence, user)',
    'mumble',
    '    Sequence __not__public.someseq does not exist'
);

SELECT * FROM check_test(
    sequence_owner_is('public', '__not__someseq', current_user, 'mumble'),
	false,
    'sequence_owner_is(sch, non-sequence, user)',
    'mumble',
    '    Sequence public.__not__someseq does not exist'
);

SELECT * FROM check_test(
    sequence_owner_is('someseq', current_user, 'mumble'),
	true,
    'sequence_owner_is(sequence, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    sequence_owner_is('someseq', current_user),
	true,
    'sequence_owner_is(sequence, user)',
    'Sequence someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    sequence_owner_is('__not__someseq', current_user, 'mumble'),
	false,
    'sequence_owner_is(non-sequence, user)',
    'mumble',
    '    Sequence __not__someseq does not exist'
);

-- It should ignore the view.
SELECT * FROM check_test(
    sequence_owner_is('public', 'someview', current_user, 'mumble'),
	false,
    'sequence_owner_is(sch, view, user, desc)',
    'mumble',
    '    Sequence public.someview does not exist'
);

SELECT * FROM check_test(
    sequence_owner_is('someview', current_user, 'mumble'),
	false,
    'sequence_owner_is(view, user, desc)',
    'mumble',
    '    Sequence someview does not exist'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;

