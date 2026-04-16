\unset ECHO
\i test/setup.sql
-- \i sql/pgtap.sql

SELECT plan(276);
-- SELECT * from no_plan();

CREATE TYPE public."myType" AS (
    id INT,
    foo INT
);

CREATE SCHEMA hidden;
CREATE TYPE hidden.stuff AS (
    id INT,
    foo INT
);

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2) DEFAULT NULL,
    "myNum" NUMERIC(8) DEFAULT 24,
    myat    TIMESTAMP DEFAULT NOW(),
    cuser   TEXT DEFAULT CURRENT_USER,
    suser   TEXT DEFAULT SESSION_USER,
    auser   TEXT DEFAULT USER,
    crole   TEXT DEFAULT CURRENT_ROLE,
    csch    TEXT DEFAULT CURRENT_SCHEMA,
    ccat    TEXT DEFAULT CURRENT_CATALOG,
    cdate   DATE DEFAULT CURRENT_DATE,
    ctime   TIMETZ DEFAULT CURRENT_TIME,
    ctstz   TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
    ltime   TIME DEFAULT LOCALTIME,
    ltstz   TIMESTAMPTZ DEFAULT LOCALTIMESTAMP,
    plain   INTEGER,
    camel   "myType",
    inval   INTERVAL(0),
    isecd   INTERVAL SECOND(0),
    iyear   INTERVAL YEAR,
    stuff   hidden.stuff
);

CREATE OR REPLACE FUNCTION fakeout( eok boolean, name text )
RETURNS SETOF TEXT AS $$
DECLARE
    descr text := coalesce( name || ' ', 'Test ' ) || 'should ';
BEGIN
    RETURN NEXT pass(descr || CASE eok WHEN true then 'pass' ELSE 'fail' END);
    RETURN NEXT pass(descr || 'have the proper description');
    RETURN NEXT pass(descr || 'have the proper diagnostics');
    RETURN;
END;
$$ LANGUAGE PLPGSQL;

RESET client_min_messages;

/****************************************************************************/
-- Test col_not_null().
SELECT * FROM check_test(
    col_not_null( 'pg_catalog', 'pg_type', 'typname', 'typname not null' ),
    true,
    'col_not_null( sch, tab, col, desc )',
    'typname not null',
    ''
);
SELECT * FROM check_test(
    col_not_null( 'pg_catalog', 'pg_type', 'typname'::name ),
    true,
    'col_not_null( sch, tab, col::name )',
    'Column pg_catalog.pg_type.typname should be NOT NULL',
    ''
);

SELECT * FROM check_test(
    col_not_null( 'sometab', 'id', 'blah blah blah' ),
    true,
    'col_not_null( tab, col, desc )',
    'blah blah blah',
    ''
);

SELECT * FROM check_test(
    col_not_null( 'sometab', 'id' ),
    true,
    'col_not_null( table, column )',
    'Column sometab.id should be NOT NULL',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_not_null( 'sometab', 'name' ),
    false,
    'col_not_null( table, column ) fail',
    'Column sometab.name should be NOT NULL',
    ''
);

-- Make sure nonexisting column is correct
SELECT * FROM check_test(
    col_not_null( 'pg_catalog', 'pg_type', 'foo', 'desc' ),
    false,
    'col_not_null( sch, tab, noncol, desc )',
    'desc',
    '    Column pg_catalog.pg_type.foo does not exist'
);

SELECT * FROM check_test(
    col_not_null( 'sometab', 'foo' ),
    false,
    'col_not_null( table, noncolumn ) fail',
    'Column sometab.foo should be NOT NULL',
    '    Column sometab.foo does not exist'
);

/****************************************************************************/
-- Test col_is_null().
SELECT * FROM check_test(
    col_is_null( 'public', 'sometab', 'name', 'name is null' ),
    true,
    'col_is_null( sch, tab, col, desc )',
    'name is null',
    ''
);

SELECT * FROM check_test(
    col_is_null( 'public', 'sometab', 'name'::name ),
    true,
    'col_is_null( sch, tab, col::name )',
    'Column public.sometab.name should allow NULL',
    ''
);

SELECT * FROM check_test(
    col_is_null( 'sometab', 'name', 'my desc' ),
    true,
    'col_is_null( tab, col, desc )',
    'my desc',
    ''
);

SELECT * FROM check_test(
    col_is_null( 'sometab', 'name' ),
    true,
    'col_is_null( tab, col )',
    'Column sometab.name should allow NULL',
    ''
);
-- Make sure failure is correct.
SELECT * FROM check_test(
    col_is_null( 'sometab', 'id' ),
    false,
    'col_is_null( tab, col ) fail',
    'Column sometab.id should allow NULL',
    ''
);

-- Make sure nonexisting column is correct
SELECT * FROM check_test(
    col_is_null( 'pg_catalog', 'pg_type', 'foo', 'desc' ),
    false,
    'col_is_null( sch, tab, noncol, desc )',
    'desc',
    '    Column pg_catalog.pg_type.foo does not exist'
);

SELECT * FROM check_test(
    col_is_null( 'sometab', 'foo' ),
    false,
    'col_is_null( table, noncolumn ) fail',
    'Column sometab.foo should allow NULL',
    '    Column sometab.foo does not exist'
);

/****************************************************************************/
-- Test col_type_is().
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'ctstz', 'pg_catalog', 'timestamptz', 'ctstz is tstz' ),
    true,
    'col_type_is( sch, tab, col, sch, type, desc )',
    'ctstz is tstz',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'ctstz', 'pg_catalog'::name, 'timestamptz' ),
    true,
    'col_type_is( sch, tab, col, sch, type, desc )',
    'Column public.sometab.ctstz should be type pg_catalog.timestamptz',
    ''
);

-- Try case-sensitive column name.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'myNum', 'pg_catalog', 'numeric(8,0)', 'myNum is numeric' ),
    true,
    'col_type_is( sch, tab, myNum, sch, type, desc )',
    'myNum is numeric',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'myNum', 'pg_catalog'::name, 'numeric(8,0)' ),
    true,
    'col_type_is( sch, tab, myNum, sch, type, desc )',
    'Column public.sometab."myNum" should be type pg_catalog.numeric(8,0)',
    ''
);

-- Try case-sensitive type name.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'camel', 'public', '"myType"', 'camel is myType' ),
    true,
    'col_type_is( sch, tab, camel, sch, type, desc )',
    'camel is myType',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'camel', 'public'::name, '"myType"' ),
    true,
    'col_type_is( sch, tab, camel, sch, type )',
    'Column public.sometab.camel should be type public."myType"',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'camel', '"myType"', 'whatever' ),
    true,
    'col_type_is( sch, tab, camel, type, desc )',
    'whatever',
    ''
);

-- Try interval.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'inval', 'pg_catalog', 'interval(0)', 'inval is interval(0)' ),
    true,
    'col_type_is( sch, tab, interval, sch, type, desc )',
    'inval is interval(0)',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'inval', 'pg_catalog'::name, 'interval(0)' ),
    true,
    'col_type_is( sch, tab, interval, sch, type, desc )',
    'Column public.sometab.inval should be type pg_catalog.interval(0)',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'inval', 'interval(0)', 'whatever' ),
    true,
    'col_type_is( sch, tab, inval, type, desc )',
    'whatever',
    ''
);

-- Try interval second.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'isecd', 'pg_catalog', 'interval second(0)', 'isecd is interval second(0)' ),
    true,
    'col_type_is( sch, tab, intsec, sch, type, desc )',
    'isecd is interval second(0)',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'inval', 'pg_catalog'::name, 'interval(0)' ),
    true,
    'col_type_is( sch, tab, interval, sch, type, desc )',
    'Column public.sometab.inval should be type pg_catalog.interval(0)',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'inval', 'interval(0)', 'whatever' ),
    true,
    'col_type_is( sch, tab, inval, type, desc )',
    'whatever',
    ''
);

-- Try type not in search path.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'stuff', 'hidden', 'stuff', 'stuff is stuff' ),
    true,
    'col_type_is( sch, tab, stuff, sch, type, desc )',
    'stuff is stuff',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'stuff', 'hidden'::name, 'stuff' ),
    true,
    'col_type_is( sch, tab, stuff, sch, type, desc )',
    'Column public.sometab.stuff should be type hidden.stuff',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'pg_catalog', 'int', 'whatever' ),
    false,
    'col_type_is( sch, tab, col, sch, type, desc ) fail',
    'whatever',
    '        have: pg_catalog.text
        want: pg_catalog.integer'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'pg_catalog', 'blech', 'whatever' ),
    false,
    'col_type_is( sch, tab, col, sch, non-type, desc )',
    'whatever',
    '    Type pg_catalog.blech does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'fooey', 'text', 'whatever' ),
    false,
    'col_type_is( sch, tab, col, non-sch, type, desc )',
    'whatever',
    '    Type fooey.text does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'fooey', 'text', 'whatever' ),
    false,
    'col_type_is( sch, tab, col, non-sch, type, desc )',
    'whatever',
    '    Type fooey.text does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'nonesuch', 'whatever' ),
    false,
    'col_type_is( sch, tab, col, non-type, desc )',
    'whatever',
    '    Type nonesuch does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'nonesuch', 'whatever' ),
    false,
    'col_type_is( tab, col, non-type, desc )',
    'whatever',
    '    Type nonesuch does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'nonesuch', 'pg_catalog', 'text', 'whatever' ),
    false,
    'col_type_is( sch, tab, non-col, sch, type, desc )',
    'whatever',
    '   Column public.sometab.nonesuch does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name', 'text', 'name is text' ),
    true,
    'col_type_is( sch, tab, col, type, desc )',
    'name is text',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'name'::name, 'text' ),
    true,
    'col_type_is( sch, tab, col, type )',
    'Column public.sometab.name should be type text',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'text', 'yadda yadda yadda' ),
    true,
    'col_type_is( tab, col, type, desc )',
    'yadda yadda yadda',
    ''
);

SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'text' ),
    true,
    'col_type_is( tab, col, type )',
    'Column sometab.name should be type text',
    ''
);

-- Make sure failure is correct.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'name', 'int4' ),
    false,
    'col_type_is( tab, col, type ) fail',
    'Column sometab.name should be type int4',
    '        have: text
        want: integer'
);

-- Make sure missing column is in diagnostics.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'blah', 'int4' ),
    false,
    'col_type_is( tab, noncol, type ) fail',
    'Column sometab.blah should be type int4',
    '   Column sometab.blah does not exist'
);

SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'blah', 'text', 'blah is text' ),
    false,
    'col_type_is( sch, tab, noncol, type, desc ) fail',
    'blah is text',
    '   Column public.sometab.blah does not exist'
);

/****************************************************************************/
-- Try col_type_is() with precision.
SELECT * FROM check_test(
    col_type_is( 'public', 'sometab', 'numb', 'numeric(10,2)', 'lol' ),
    true,
    'col_type_is with precision',
    'lol',
    ''
);

-- Check its diagnostics.
SELECT * FROM check_test(
    col_type_is( 'sometab', 'myNum', 'numeric(7)', 'should be numeric(7)' ),
    false,
    'col_type_is precision fail',
    'should be numeric(7)',
    '        have: numeric(8,0)
        want: numeric(7,0)'
);

/****************************************************************************/
-- Test col_has_default().
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', 'name', 'desc' ),
    true,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'name', 'desc' ),
    true,
    'col_has_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'name' ),
    true,
    'col_has_default( tab, col )',
    'Column sometab.name should have a default',
    ''
);

-- Check with a column with no default.
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', 'plain', 'desc' ),
    false,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'plain', 'desc' ),
    false,
    'col_has_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_has_default( 'sometab', 'plain' ),
    false,
    'col_has_default( tab, col )',
    'Column sometab.plain should have a default',
    ''
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_has_default( 'public', 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_has_default( sch, tab, col, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_has_default( 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_has_default( tab, col, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_has_default( 'sometab', '__asdfasdfs__' ),
    false,
    'col_has_default( tab, col )',
    'Column sometab.__asdfasdfs__ should have a default',
    '    Column sometab.__asdfasdfs__ does not exist'
);

/****************************************************************************/
-- Test col_hasnt_default().
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', 'name', 'desc' ),
    false,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'name', 'desc' ),
    false,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'name' ),
    false,
    'col_hasnt_default( tab, col )',
    'Column sometab.name should not have a default',
    ''
);

-- Check with a column with no default.
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', 'plain', 'desc' ),
    true,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'plain', 'desc' ),
    true,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', 'plain' ),
    true,
    'col_hasnt_default( tab, col )',
    'Column sometab.plain should not have a default',
    ''
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_hasnt_default( 'public', 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_hasnt_default( sch, tab, col, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', '__asdfasdfs__', 'desc' ),
    false,
    'col_hasnt_default( tab, col, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_hasnt_default( 'sometab', '__asdfasdfs__' ),
    false,
    'col_hasnt_default( tab, col )',
    'Column sometab.__asdfasdfs__ should not have a default',
    '    Column sometab.__asdfasdfs__ does not exist'
);

/****************************************************************************/
-- Test col_default_is().

SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'name', ''::text, 'name should default to empty string' ),
    true,
    'col_default_is( sch, tab, col, def, desc )',
    'name should default to empty string',
    ''
);

SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'name', 'foo'::text, 'name should default to ''foo''' ),
    false,
    'col_default_is() fail',
    'name should default to ''foo''',
    '        have: 
        want: foo'
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'name', ''::text, 'name should default to empty string' ),
    true,
    'col_default_is( tab, col, def, desc )',
    'name should default to empty string',
    ''
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'name', ''::text ),
    true,
    'col_default_is( tab, col, def )',
    'Column sometab.name should default to ''''',
    ''
);

-- Make sure it works with a NULL default.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myNum', 24 ),
    true,
    'col_default_is( tab, col, int )',
    'Column sometab."myNum" should default to ''24''',
    ''
);

        -- We can handle DEFAULT NULL correctly.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'numb', NULL::numeric, 'desc' ),
    true,
    'col_default_is( tab, col, NULL, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'numb', NULL::numeric ),
    true,
    'col_default_is( tab, col, NULL )',
    'Column sometab.numb should default to NULL',
    ''
);

-- Make sure that it fails when there is no default.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'plain', 1, 'desc' ),
    false,
    'col_default_is( tab, col, bogus, desc )',
    'desc',
    '    Column sometab.plain has no default'
);

SELECT * FROM check_test(
    col_default_is( 'sometab', 'plain', 1 ),
    false,
    'col_default_is( tab, col, bogus )',
    'Column sometab.plain should default to ''1''',
    '    Column sometab.plain has no default'
);

-- Make sure that it works when the default is an expression.
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myat', 'now()' ),
    true,
    'col_default_is( tab, col, expression )',
    'Column sometab.myat should default to ''now()''',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myat', 'now()'::text ),
    true,
    'col_default_is( tab, col, expression::text )',
    'Column sometab.myat should default to ''now()''',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myat', 'now()', 'desc' ),
    true,
    'col_default_is( tab, col, expression, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'sometab', 'myat', 'now()', 'desc'::text ),
    true,
    'col_default_is( tab, col, expression, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'myat', 'now()', 'desc' ),
    true,
    'col_default_is( schema, tab, col, expression, desc )',
    'desc',
    ''
);
SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', 'myat', 'now()', 'desc'::text ),
    true,
    'col_default_is( schema, tab, col, expression, desc )',
    'desc',
    ''
);

-- Check with a nonexistent column.
SELECT * FROM check_test(
    col_default_is( 'public', 'sometab', '__asdfasdfs__', NULL::text, 'desc' ),
    false,
    'col_default_is( sch, tab, col, def, desc )',
    'desc',
    '    Column public.sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_default_is( 'sometab', '__asdfasdfs__', NULL::text, 'desc' ),
    false,
    'col_default_is( tab, col, def, desc )',
    'desc',
    '    Column sometab.__asdfasdfs__ does not exist'
);
SELECT * FROM check_test(
    col_default_is( 'sometab', '__asdfasdfs__', NULL::text ),
    false,
    'col_default_is( tab, col, def )',
    'Column sometab.__asdfasdfs__ should default to NULL',
    '    Column sometab.__asdfasdfs__ does not exist'
);

-- Make sure that it works when the default is a reserved SQL expression.
CREATE OR REPLACE FUNCTION ckreserve() RETURNS SETOF TEXT LANGUAGE PLPGSQL AS $$
DECLARE
    funcs text[] := '{CURRENT_CATALOG,CURRENT_ROLE,CURRENT_SCHEMA,CURRENT_USER,SESSION_USER,USER,CURRENT_DATE,CURRENT_TIME,CURRENT_TIMESTAMP,LOCALTIME,LOCALTIMESTAMP}';
    cols  TEXT[] := '{ccat,crole,csch,cuser,suser,auser,cdate,ctime,ctstz,ltime,ltstz}';
    exp   TEXT[] := funcs;
    tap           record;
    last_index    INTEGER;
BEGIN
    last_index := array_upper(funcs, 1);
    IF pg_version_num() < 100000 THEN
       -- Prior to PostgreSQL 10, these were functions rendered with paretheses or as casts.
       exp := ARRAY['current_database()','"current_user"()','"current_schema"()','"current_user"()','"session_user"()','"current_user"()','(''now''::text)::date','(''now''::text)::time with time zone','now()','(''now''::text)::time without time zone','(''now''::text)::timestamp without time zone'];
    END IF;

    FOR i IN 1..last_index LOOP
        FOR tap IN SELECT * FROM check_test(
            col_default_is( 'sometab', cols[i], exp[i], 'Test ' || funcs[i] ),
            true,
            'col_default_is( tab, col, ' || funcs[i] || ' )',
            'Test ' || funcs[i],
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    END LOOP;
END;
$$;
SELECT * FROM ckreserve();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
