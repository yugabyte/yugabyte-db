\unset ECHO
\i test/setup.sql

SELECT plan(411);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2),
    "myInt" NUMERIC(8)
);

CREATE INDEX idx_hey ON public.sometab(numb);

-- Create a partition.
CREATE FUNCTION mkpart() RETURNS SETOF TEXT AS $$
BEGIN
    IF pg_version_num() >= 100000 THEN
        EXECUTE $E$
            CREATE TABLE public.apart (dt DATE NOT NULL) PARTITION BY RANGE (dt);
        $E$;
    ELSE
    EXECUTE $E$
        CREATE TABLE public.apart (dt DATE NOT NULL);
    $E$;
    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;
SELECT * FROM mkpart();

CREATE VIEW public.someview AS SELECT * FROM public.sometab;

CREATE TYPE public.sometype AS (
    id    INT,
    name  TEXT
);

CREATE SEQUENCE public.someseq;

CREATE SCHEMA someschema;
CREATE TABLE someschema.anothertab(
    id INT PRIMARY KEY,
    name    TEXT DEFAULT ''
);

CREATE DOMAIN someschema.us_postal_code AS TEXT CHECK(
    VALUE ~ '^[[:digit:]]{5}$' OR VALUE ~ '^[[:digit:]]{5}-[[:digit:]]{4}$'
);

CREATE INDEX idx_name ON someschema.anothertab(name);

CREATE FUNCTION public.somefunction(int) RETURNS VOID LANGUAGE SQL AS '';

RESET client_min_messages;

/****************************************************************************/
-- Test db_owner_is().
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
    'Database ' || quote_ident(current_database()) || ' should be owned by ' || quote_ident(current_user),
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
-- Test schema_owner_is().
SELECT * FROM check_test(
    schema_owner_is(current_schema(), _get_schema_owner(current_schema()), 'mumble'),
	true,
    'schema_owner_is(schema, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    schema_owner_is(current_schema(), _get_schema_owner(current_schema())),
	true,
    'schema_owner_is(schema, user)',
    'Schema ' || quote_ident(current_schema()) || ' should be owned by ' || _get_schema_owner(current_schema()),
    ''
);

SELECT * FROM check_test(
    schema_owner_is('__not__' || current_schema(), _get_schema_owner(current_schema()), 'mumble'),
	false,
    'schema_owner_is(non-schema, user)',
    'mumble',
    '    Schema __not__' || current_schema() || ' does not exist'
);

SELECT * FROM check_test(
    schema_owner_is(current_schema(), '__not__' || _get_schema_owner(current_schema()), 'mumble'),
	false,
    'schema_owner_is(schema, non-user)',
    'mumble',
    '        have: ' || _get_schema_owner(current_schema()) || '
        want: __not__' || _get_schema_owner(current_schema())
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
    'Relation public.sometab should be owned by ' || quote_ident(current_user),
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
    'Relation sometab should be owned by ' || quote_ident(current_user),
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
-- Test relation_owner_is() with a partition.
SELECT * FROM check_test(
    relation_owner_is('public', 'apart', current_user, 'mumble'),
	true,
    'relation_owner_is(sch, part, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('public', 'apart', current_user),
	true,
    'relation_owner_is(sch, part, user)',
    'Relation public.apart should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__public', 'apart', current_user, 'mumble'),
	false,
    'relation_owner_is(non-sch, part, user)',
    'mumble',
    '    Relation __not__public.apart does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('public', '__not__apart', current_user, 'mumble'),
	false,
    'relation_owner_is(sch, non-part, user)',
    'mumble',
    '    Relation public.__not__apart does not exist'
);

SELECT * FROM check_test(
    relation_owner_is('apart', current_user, 'mumble'),
	true,
    'relation_owner_is(part, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    relation_owner_is('apart', current_user),
	true,
    'relation_owner_is(part, user)',
    'Relation apart should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    relation_owner_is('__not__apart', current_user, 'mumble'),
	false,
    'relation_owner_is(non-part, user)',
    'mumble',
    '    Relation __not__apart does not exist'
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
    'Relation public.someseq should be owned by ' || quote_ident(current_user),
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
    'Relation someseq should be owned by ' || quote_ident(current_user),
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
    'Table public.sometab should be owned by ' || quote_ident(current_user),
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
    'Table sometab should be owned by ' || quote_ident(current_user),
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

-- But not a partition.
SELECT * FROM check_test(
    table_owner_is('public', 'apart', current_user, 'mumble'),
	true,
    'table_owner_is(sch, part, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    table_owner_is('apart', current_user, 'mumble'),
	true,
    'table_owner_is(part, user, desc)',
    'mumble',
    ''
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
    'View public.someview should be owned by ' || quote_ident(current_user),
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
    'View someview should be owned by ' || quote_ident(current_user),
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
    'Sequence public.someseq should be owned by ' || quote_ident(current_user),
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
    'Sequence someseq should be owned by ' || quote_ident(current_user),
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
    'Composite type public.sometype should be owned by ' || quote_ident(current_user),
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
    'Composite type sometype should be owned by ' || quote_ident(current_user),
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
CREATE FUNCTION public.test_fdw() RETURNS SETOF TEXT AS $$
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
            'Foreign table public.my_fdw should be owned by ' || quote_ident(current_user),
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
            'Foreign table my_fdw should be owned by ' || quote_ident(current_user),
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

SELECT * FROM public.test_fdw();

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
    'Function public.somefunction(integer) should be owned by ' || quote_ident(current_user),
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
    'Function public.test_fdw() should be owned by ' || quote_ident(current_user),
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
    'Function somefunction(integer) should be owned by ' || quote_ident(current_user),
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
    'Function test_fdw() should be owned by ' || quote_ident(current_user),
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
-- Test tablespace_owner_is().

SELECT * FROM check_test(
    tablespace_owner_is('pg_default', _get_tablespace_owner('pg_default'), 'mumble'),
	true,
    'tablespace_owner_is(tablespace, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    tablespace_owner_is('pg_default', _get_tablespace_owner('pg_default')),
	true,
    'tablespace_owner_is(tablespace, user)',
    'Tablespace ' || quote_ident('pg_default') || ' should be owned by ' || _get_tablespace_owner('pg_default'),
    ''
);

SELECT * FROM check_test(
    tablespace_owner_is('__not__' || 'pg_default', _get_tablespace_owner('pg_default'), 'mumble'),
	false,
    'tablespace_owner_is(non-tablespace, user)',
    'mumble',
    '    Tablespace __not__' || 'pg_default' || ' does not exist'
);

SELECT * FROM check_test(
    tablespace_owner_is('pg_default', '__not__' || _get_tablespace_owner('pg_default'), 'mumble'),
	false,
    'tablespace_owner_is(tablespace, non-user)',
    'mumble',
    '        have: ' || _get_tablespace_owner('pg_default') || '
        want: __not__' || _get_tablespace_owner('pg_default')
);

/****************************************************************************/
-- Test index_owner_is().
SELECT * FROM check_test(
    index_owner_is('someschema', 'anothertab', 'idx_name', current_user, 'mumble'),
	true,
    'index_owner_is(schema, table, index, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    index_owner_is('someschema', 'anothertab', 'idx_name', current_user),
	true,
    'index_owner_is(schema, table, index, user)',
    'Index idx_name ON someschema.anothertab should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    index_owner_is('someschema', 'anothertab', 'idx_foo', current_user, 'mumble'),
	false,
    'index_owner_is(schema, table, non-index, user, desc)',
    'mumble',
    '    Index idx_foo ON someschema.anothertab not found'
);

SELECT * FROM check_test(
    index_owner_is('someschema', 'nonesuch', 'idx_name', current_user, 'mumble'),
	false,
    'index_owner_is(schema, non-table, index, user, desc)',
    'mumble',
    '    Index idx_name ON someschema.nonesuch not found'
);

SELECT * FROM check_test(
    index_owner_is('nonesuch', 'anothertab', 'idx_name', current_user, 'mumble'),
	false,
    'index_owner_is(non-schema, table, index, user, desc)',
    'mumble',
    '    Index idx_name ON nonesuch.anothertab not found'
);

SELECT * FROM check_test(
    index_owner_is('someschema', 'anothertab', 'idx_name', '__noone', 'mumble'),
	false,
    'index_owner_is(schema, table, index, non-user, desc)',
    'mumble',
    '        have: ' || current_user || '
        want: __noone'
);

SELECT * FROM check_test(
    index_owner_is('anothertab', 'idx_name', current_user, 'mumble'),
	false,
    'index_owner_is(invisible-table, index, user, desc)',
    'mumble',
    '    Index idx_name ON anothertab not found'
);

SELECT * FROM check_test(
    index_owner_is('sometab', 'idx_hey', current_user, 'mumble'),
	true,
    'index_owner_is(table, index, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    index_owner_is('sometab', 'idx_hey', current_user),
	true,
    'index_owner_is(table, index, user)',
    'Index idx_hey ON sometab should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    index_owner_is('notab', 'idx_hey', current_user, 'mumble'),
	false,
    'index_owner_is(non-table, index, user)',
    'mumble',
    '    Index idx_hey ON notab not found'
);

SELECT * FROM check_test(
    index_owner_is('sometab', 'idx_foo', current_user, 'mumble'),
	false,
    'index_owner_is(table, non-index, user)',
    'mumble',
    '    Index idx_foo ON sometab not found'
);

SELECT * FROM check_test(
    index_owner_is('sometab', 'idx_hey', '__no-one', 'mumble'),
	false,
    'index_owner_is(table, index, non-user)',
    'mumble',
    '        have: ' || current_user || '
        want: __no-one'
);

/****************************************************************************/
-- Test language_owner_is().
CREATE FUNCTION test_lang() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 80300 THEN
        FOR tap IN SELECT * FROM check_test(
            language_owner_is('plpgsql', _get_language_owner('plpgsql'), 'mumble'),
	        true,
            'language_owner_is(language, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            language_owner_is('plpgsql', _get_language_owner('plpgsql')),
	        true,
            'language_owner_is(language, user)',
            'Language ' || quote_ident('plpgsql') || ' should be owned by ' || _get_language_owner('plpgsql'),
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            language_owner_is('__not__plpgsql', _get_language_owner('plpgsql'), 'mumble'),
	        false,
            'language_owner_is(non-language, user)',
            'mumble',
            '    Language __not__plpgsql does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            language_owner_is('plpgsql', '__not__' || _get_language_owner('plpgsql'), 'mumble'),
	        false,
            'language_owner_is(language, non-user)',
            'mumble',
            '        have: ' || _get_language_owner('plpgsql') || '
        want: __not__' || _get_language_owner('plpgsql')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'language_owner_is(language, user, desc)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('mumble'),
	        true,
            'language_owner_is(language, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'language_owner_is(non-language, user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('mumble'),
	        false,
            'language_owner_is(language, non-user)',
            'mumble',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    END IF;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_lang();

/****************************************************************************/
-- Test opclass_owner_is().
SELECT * FROM check_test(
    opclass_owner_is(
        'pg_catalog', 'int4_ops',
        _get_opclass_owner('pg_catalog', 'int4_ops'),
        'mumble'
    ),
	true,
    'opclass_owner_is(schema, opclass, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    opclass_owner_is(
        'pg_catalog', 'int4_ops',
        _get_opclass_owner('pg_catalog', 'int4_ops')
    ),
	true,
    'opclass_owner_is(schema, opclass, user)',
    'Operator class pg_catalog.int4_ops should be owned by ' || _get_opclass_owner('pg_catalog', 'int4_ops'),
    ''
);

SELECT * FROM check_test(
    opclass_owner_is(
        'not_pg_catalog', 'int4_ops',
        _get_opclass_owner('pg_catalog', 'int4_ops'),
        'mumble'
    ),
	false,
    'opclass_owner_is(non-schema, opclass, user, desc)',
    'mumble',
    '    Operator class not_pg_catalog.int4_ops not found'
);

SELECT * FROM check_test(
    opclass_owner_is(
        'pg_catalog', 'int4_nots',
        _get_opclass_owner('pg_catalog', 'int4_ops'),
        'mumble'
    ),
	false,
    'opclass_owner_is(schema, not-opclass, user, desc)',
    'mumble',
    '    Operator class pg_catalog.int4_nots not found'
);

SELECT * FROM check_test(
    opclass_owner_is(
        'pg_catalog', 'int4_ops', '__no-one', 'mumble'
    ),
	false,
    'opclass_owner_is(schema, opclass, non-user, desc)',
    'mumble',
    '        have: ' || _get_opclass_owner('pg_catalog', 'int4_ops') || '
        want: __no-one'
);

SELECT * FROM check_test(
    opclass_owner_is('int4_ops', _get_opclass_owner('int4_ops'), 'mumble'),
	true,
    'opclass_owner_is(opclass, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    opclass_owner_is('int4_ops', _get_opclass_owner('int4_ops')),
	true,
    'opclass_owner_is(opclass, user)',
    'Operator class int4_ops should be owned by ' || _get_opclass_owner('int4_ops'),
    ''
);

SELECT * FROM check_test(
    opclass_owner_is('int4_nots', _get_opclass_owner('int4_ops'), 'mumble'),
	false,
    'opclass_owner_is(non-opclass, user, desc)',
    'mumble',
    '    Operator class int4_nots not found'
);

SELECT * FROM check_test(
    opclass_owner_is('int4_ops', '__no-one', 'mumble'),
	false,
    'opclass_owner_is(opclass, non-user, desc)',
    'mumble',
    '        have: ' || _get_opclass_owner('int4_ops') || '
        want: __no-one'
);

/****************************************************************************/
-- Test type_owner_is() with a table.
SELECT * FROM check_test(
    type_owner_is('someschema', 'us_postal_code', current_user, 'mumble'),
	true,
    'type_owner_is(schema, type, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    type_owner_is('someschema', 'us_postal_code', current_user),
	true,
    'type_owner_is(schema, type, user)',
    'Type someschema.us_postal_code should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    type_owner_is('--nonesuch', 'us_postal_code', current_user, 'mumble'),
	false,
    'type_owner_is(non-schema, type, user, desc)',
    'mumble',
    '    Type "--nonesuch".us_postal_code not found'
);

SELECT * FROM check_test(
    type_owner_is('someschema', 'uk_postal_code', current_user, 'mumble'),
	false,
    'type_owner_is(schema, non-type, user, desc)',
    'mumble',
    '    Type someschema.uk_postal_code not found'
);

SELECT * FROM check_test(
    type_owner_is('someschema', 'us_postal_code', '__no-one', 'mumble'),
	false,
    'type_owner_is(schema, type, non-user, desc)',
    'mumble',
    '        have: ' || current_user || '
        want: __no-one'
);

SELECT * FROM check_test(
    type_owner_is('us_postal_code', current_user, 'mumble'),
	false,
    'type_owner_is( invisible-type, user, desc)',
    'mumble',
    '    Type us_postal_code not found'
);

SELECT * FROM check_test(
    type_owner_is('sometype', current_user, 'mumble'),
	true,
    'type_owner_is(type, user, desc)',
    'mumble',
    ''
);

SELECT * FROM check_test(
    type_owner_is('sometype', current_user),
	true,
    'type_owner_is(type, user)',
    'Type sometype should be owned by ' || quote_ident(current_user),
    ''
);

SELECT * FROM check_test(
    type_owner_is('__notype', current_user, 'mumble'),
	false,
    'type_owner_is(non-type, user, desc)',
    'mumble',
    '    Type __notype not found'
);

SELECT * FROM check_test(
    type_owner_is('sometype', '__no-one', 'mumble'),
	false,
    'type_owner_is(type, non-user, desc)',
    'mumble',
    '        have: ' || current_user || '
        want: __no-one'
);

/****************************************************************************/
-- Test materialized_view_owner_is().
CREATE FUNCTION test_materialized_view_owner_is() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 93000 THEN
        EXECUTE $E$
            CREATE MATERIALIZED VIEW public.somemview AS SELECT * FROM public.sometab;
        $E$;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('public', 'somemview', current_user, 'mumble'),
            true,
            'materialized_view_owner_is(sch, materialized_view, user, desc)',
            'mumble',
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('public', 'somemview', current_user),
            true,
            'materialized_view_owner_is(sch, materialized_view, user)',
            'Materialized view public.somemview should be owned by ' || quote_ident(current_user),
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('__not__public', 'somemview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(non-sch, materialized_view, user)',
            'mumble',
            '    Materialized view __not__public.somemview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('public', '__not__somemview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(sch, non-materialized_view, user)',
            'mumble',
            '    Materialized view public.__not__somemview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('somemview', current_user, 'mumble'),
            true,
            'materialized_view_owner_is(materialized_view, user, desc)',
            'mumble',
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('somemview', current_user),
            true,
            'materialized_view_owner_is(view, user)',
            'Materialized view somemview should be owned by ' || quote_ident(current_user),
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('__not__somemview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(non-materialized_view, user)',
            'mumble',
            '    Materialized view __not__somemview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        -- It should ignore the sequence.
        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('public', 'someseq', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(sch, seq, user, desc)',
            'mumble',
            '    Materialized view public.someseq does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_view_owner_is('someseq', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(seq, user, desc)',
            'mumble',
            '    Materialized view someseq does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;
    else

    FOR tap IN SELECT * FROM check_test(
            view_owner_is('public', 'someview', current_user, 'mumble'),
            true,
            'materialized_view_owner_is(sch, materialized_view, user, desc)',
            'mumble',
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('public', 'someview', current_user),
            true,
            'materialized_view_owner_is(sch, materialized_view, user)',
            'View public.someview should be owned by ' || quote_ident(current_user),
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('__not__public', 'someview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(non-sch, materialized_view, user)',
            'mumble',
            '    View __not__public.someview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('public', '__not__someview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(sch, non-materialized_view, user)',
            'mumble',
            '    View public.__not__someview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('someview', current_user, 'mumble'),
            true,
            'materialized_view_owner_is(materialized_view, user, desc)',
            'mumble',
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('someview', current_user),
            true,
            'materialized_view_owner_is(view, user)',
            'View someview should be owned by ' || quote_ident(current_user),
            ''
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('__not__someview', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(non-materialized_view, user)',
            'mumble',
            '    View __not__someview does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        -- It should ignore the sequence.
        FOR tap IN SELECT * FROM check_test(
            view_owner_is('public', 'someseq', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(sch, seq, user, desc)',
            'mumble',
            '    View public.someseq does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

        FOR tap IN SELECT * FROM check_test(
            view_owner_is('someseq', current_user, 'mumble'),
            false,
            'materialized_view_owner_is(seq, user, desc)',
            'mumble',
            '    View someseq does not exist'
        ) AS b LOOP
                    RETURN NEXT tap.b;
                END LOOP;

    end if;

    return;

end; $$language PLPGSQL;

SELECT * from test_materialized_view_owner_is();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
