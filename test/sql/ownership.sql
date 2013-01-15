\unset ECHO
\i test/setup.sql

SELECT plan(228);
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

CREATE FUNCTION public.somefunction(int) RETURNS VOID LANGUAGE SQL AS '';

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
-- Test relation_owner_is() with a table.
SELECT * FROM check_test(
    relation_owner_is('public', 'sometab', current_user, 'mumble'),
	true,
    'relation_owner_is(sch, tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('public', 'sometab', current_user),
	true,
    'relation_owner_is(sch, tab, user)',
    'Relation public.sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__public', 'sometab', current_user, 'mumble'),
	false,
    'relation_owner_is(non-sch, tab, user)',
    'mumble',
    '    Relation __not__public.sometab does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('public', '__not__sometab', current_user, 'mumble'),
	false,
    'relation_owner_is(sch, non-tab, user)',
    'mumble',
    '    Relation public.__not__sometab does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('sometab', current_user, 'mumble'),
	true,
    'relation_owner_is(tab, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('sometab', current_user),
	true,
    'relation_owner_is(tab, user)',
    'Relation sometab should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__sometab', current_user, 'mumble'),
	false,
    'relation_owner_is(non-tab, user)',
    'mumble',
    '    Relation __not__sometab does not exist'
);

/****************************************************************************/
-- Test relation_owner_is() with a schema.
SELECT * FROM check_test(
    relation_owner_is('public', 'someseq', current_user, 'mumble'),
	true,
    'relation_owner_is(sch, seq, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('public', 'someseq', current_user),
	true,
    'relation_owner_is(sch, seq, user)',
    'Relation public.someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__public', 'someseq', current_user, 'mumble'),
	false,
    'relation_owner_is(non-sch, seq, user)',
    'mumble',
    '    Relation __not__public.someseq does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('public', '__not__someseq', current_user, 'mumble'),
	false,
    'relation_owner_is(sch, non-seq, user)',
    'mumble',
    '    Relation public.__not__someseq does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('someseq', current_user, 'mumble'),
	true,
    'relation_owner_is(seq, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('someseq', current_user),
	true,
    'relation_owner_is(seq, user)',
    'Relation someseq should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__someseq', current_user, 'mumble'),
	false,
    'relation_owner_is(non-seq, user)',
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
-- Test composite_owner_is().
SELECT * FROM check_test(
    composite_owner_is('public', 'sometype', current_user, 'mumble'),
	true,
    'composite_owner_is(sch, composite, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    composite_owner_is('public', 'sometype', current_user),
	true,
    'composite_owner_is(sch, composite, user)',
    'Composite type public.sometype should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    composite_owner_is('__not__public', 'sometype', current_user, 'mumble'),
	false,
    'composite_owner_is(non-sch, composite, user)',
    'mumble',
    '    Composite type __not__public.sometype does not exist'
);

SELECT * FROM check_test(
    composite_owner_is('public', '__not__sometype', current_user, 'mumble'),
	false,
    'composite_owner_is(sch, non-composite, user)',
    'mumble',
    '    Composite type public.__not__sometype does not exist'
);

SELECT * FROM check_test(
    composite_owner_is('sometype', current_user, 'mumble'),
	true,
    'composite_owner_is(composite, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    composite_owner_is('sometype', current_user),
	true,
    'composite_owner_is(composite, user)',
    'Composite type sometype should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    composite_owner_is('__not__sometype', current_user, 'mumble'),
	false,
    'composite_owner_is(non-composite, user)',
    'mumble',
    '    Composite type __not__sometype does not exist'
);

-- It should ignore the view.
SELECT * FROM check_test(
    composite_owner_is('public', 'someview', current_user, 'mumble'),
	false,
    'composite_owner_is(sch, view, user, desc)',
    'mumble',
    '    Composite type public.someview does not exist'
);

SELECT * FROM check_test(
    composite_owner_is('someview', current_user, 'mumble'),
	false,
    'composite_owner_is(view, user, desc)',
    'mumble',
    '    Composite type someview does not exist'
);

/****************************************************************************/
-- Test foreign_table_owner_is().
CREATE FUNCTION test_fdw() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 90100 THEN
        EXECUTE $E$
            CREATE FOREIGN DATA WRAPPER dummy;
            CREATE SERVER foo FOREIGN DATA WRAPPER dummy;
            CREATE FOREIGN TABLE public.my_fdw (id int) SERVER foo;
        $E$;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('public', 'my_fdw', current_user, 'mumble'),
	        true,
            'foreign_table_owner_is(sch, tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('public', 'my_fdw', current_user),
	        true,
            'foreign_table_owner_is(sch, tab, user)',
            'Foreign table public.my_fdw should be owned by ' || current_user,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('__not__public', 'my_fdw', current_user, 'mumble'),
	        false,
            'foreign_table_owner_is(non-sch, tab, user)',
            'mumble',
            '    Foreign table __not__public.my_fdw does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('public', '__not__my_fdw', current_user, 'mumble'),
	        false,
            'foreign_table_owner_is(sch, non-tab, user)',
            'mumble',
            '    Foreign table public.__not__my_fdw does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('my_fdw', current_user, 'mumble'),
	        true,
            'foreign_table_owner_is(tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('my_fdw', current_user),
	        true,
            'foreign_table_owner_is(tab, user)',
            'Foreign table my_fdw should be owned by ' || current_user,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('__not__my_fdw', current_user, 'mumble'),
	        false,
            'foreign_table_owner_is(non-tab, user)',
            'mumble',
            '    Foreign table __not__my_fdw does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- It should ignore the table.
        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('public', 'sometab', current_user, 'mumble'),
	        false,
            'foreign_table_owner_is(sch, tab, user, desc)',
            'mumble',
            '    Foreign table public.sometab does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            foreign_table_owner_is('sometab', current_user, 'mumble'),
	        false,
            'foreign_table_owner_is(tab, user, desc)',
            'mumble',
            '    Foreign table sometab does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'foreign_table_owner_is(sch, tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'foreign_table_owner_is(sch, tab, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'foreign_table_owner_is(non-sch, tab, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'foreign_table_owner_is(sch, non-tab, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'foreign_table_owner_is(tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'foreign_table_owner_is(tab, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'foreign_table_owner_is(non-tab, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- It should ignore the table.
        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'foreign_table_owner_is(sch, tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'foreign_table_owner_is(tab, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    END IF;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_fdw();

/****************************************************************************/
-- Test function_owner_is().
SELECT * FROM check_test(
    function_owner_is('public', 'somefunction', ARRAY['integer'], current_user, 'mumble'),
	true,
    'function_owner_is(sch, function, args[integer], user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    function_owner_is('public', 'somefunction', ARRAY['integer'], current_user),
	true,
    'function_owner_is(sch, function, args[integer], user)',
    'Function public.somefunction(integer) should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    function_owner_is('public', 'test_fdw', '{}'::NAME[], current_user, 'mumble'),
	true,
    'function_owner_is(sch, function, args[], user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    function_owner_is('public', 'test_fdw', '{}'::NAME[], current_user),
	true,
    'function_owner_is(sch, function, args[], user)',
    'Function public.test_fdw() should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    function_owner_is('somefunction', ARRAY['integer'], current_user, 'mumble'),
	true,
    'function_owner_is(function, args[integer], user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    function_owner_is('somefunction', ARRAY['integer'], current_user),
	true,
    'function_owner_is(function, args[integer], user)',
    'Function somefunction(integer) should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    function_owner_is('test_fdw', '{}'::NAME[], current_user, 'mumble'),
	true,
    'function_owner_is(function, args[], user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    function_owner_is('test_fdw', '{}'::NAME[], current_user),
	true,
    'function_owner_is(function, args[], user)',
    'Function test_fdw() should be owned by ' || current_user,
    ''
);

SELECT * FROM check_test(
    function_owner_is('public', '__non__function', ARRAY['integer'], current_user, 'mumble'),
	false,
    'function_owner_is(sch, non-function, args[integer], user, desc)',
    'mumble',
    '    Function public.__non__function(integer) does not exist'
);

SELECT * FROM check_test(
    function_owner_is('__non__public', 'afunction', ARRAY['integer'], current_user, 'mumble'),
	false,
    'function_owner_is(non-sch, function, args[integer], user, desc)',
    'mumble',
    '    Function __non__public.afunction(integer) does not exist'
);

SELECT * FROM check_test(
    function_owner_is('__non__function', ARRAY['integer'], current_user, 'mumble'),
	false,
    'function_owner_is(non-function, args[integer], user, desc)',
    'mumble',
    '    Function __non__function(integer) does not exist'
);

SELECT * FROM check_test(
    function_owner_is('public', 'somefunction', ARRAY['integer'], 'no one', 'mumble'),
	false,
    'function_owner_is(sch, function, args[integer], non-user, desc)',
    'mumble',
    '        have: ' || current_user || '
        want: no one'
);

SELECT * FROM check_test(
    function_owner_is('somefunction', ARRAY['integer'], 'no one', 'mumble'),
	false,
    'function_owner_is(function, args[integer], non-user, desc)',
    'mumble',
    '        have: ' || current_user || '
        want: no one'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
