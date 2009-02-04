\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(231);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE TYPE sometype AS (
    id    INT,
    name  TEXT
);

CREATE DOMAIN us_postal_code AS TEXT CHECK(
    VALUE ~ '^[[:digit:]]{5}$' OR VALUE ~ '^[[:digit:]]{5}-[[:digit:]]{4}$'
);

CREATE SCHEMA someschema;
RESET client_min_messages;

/****************************************************************************/
-- Test has_schema().
SELECT * FROM check_test(
    has_schema( '__SDFSDFD__' ),
    false,
    'has_schema(non-existent schema)',
    'Schema __SDFSDFD__ should exist',
    ''
);
SELECT * FROM check_test(
    has_schema( '__SDFSDFD__', 'lol' ),
    false,
    'has_schema(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_schema( 'someschema' ),
    true,
    'has_schema(schema)',
    'Schema someschema should exist',
    ''
);
SELECT * FROM check_test(
    has_schema( 'someschema', 'lol' ),
    true,
    'has_schema(schema, desc)',
    'lol',
    ''
);

/****************************************************************************/
-- Test hasnt_schema().
SELECT * FROM check_test(
    hasnt_schema( '__SDFSDFD__' ),
    true,
    'hasnt_schema(non-existent schema)',
    'Schema __SDFSDFD__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_schema( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_schema(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_schema( 'someschema' ),
    false,
    'hasnt_schema(schema)',
    'Schema someschema should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_schema( 'someschema', 'lol' ),
    false,
    'hasnt_schema(schema, desc)',
    'lol',
    ''
);

/****************************************************************************/
-- Test has_table().

SELECT * FROM check_test(
    has_table( '__SDFSDFD__' ),
    false,
    'has_table(non-existent table)',
    'Table __SDFSDFD__ should exist',
    ''
);

SELECT * FROM check_test(
    has_table( '__SDFSDFD__', 'lol' ),
    false,
    'has_table(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_table( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_table(sch, non-existent table, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_table( 'pg_type', 'lol' ),
    true,
    'has_table(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_table( 'pg_catalog', 'pg_type', 'desc' ),
    true,
    'has_table(sch, tab, desc)',
    'desc',
    ''
);

-- It should ignore views and types.
SELECT * FROM check_test(
    has_table( 'pg_catalog', 'pg_tables', 'desc' ),
    false,
    'has_table(sch, view, desc)',
    'desc',
    ''
);
SELECT * FROM check_test(
    has_table( 'sometype', 'desc' ),
    false,
    'has_table(type, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_table().

SELECT * FROM check_test(
    hasnt_table( '__SDFSDFD__' ),
    true,
    'hasnt_table(non-existent table)',
    'Table __SDFSDFD__ should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_table( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_table(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_table(sch, non-existent tab, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'pg_type', 'lol' ),
    false,
    'hasnt_table(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'pg_catalog', 'pg_type', 'desc' ),
    false,
    'hasnt_table(sch, tab, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test has_view().

SELECT * FROM check_test(
    has_view( '__SDFSDFD__' ),
    false,
    'has_view(non-existent view)',
    'View __SDFSDFD__ should exist',
    ''
);

SELECT * FROM check_test(
    has_view( '__SDFSDFD__', 'howdy' ),
    false,
    'has_view(non-existent view, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    has_view( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_view(sch, non-existtent view, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_view( 'pg_tables', 'yowza' ),
    true,
    'has_view(view, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    has_view( 'information_schema', 'tables', 'desc' ),
    true,
    'has_view(sch, view, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_view().

SELECT * FROM check_test(
    hasnt_view( '__SDFSDFD__' ),
    true,
    'hasnt_view(non-existent view)',
    'View __SDFSDFD__ should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_view( '__SDFSDFD__', 'howdy' ),
    true,
    'hasnt_view(non-existent view, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_view(sch, non-existtent view, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'pg_tables', 'yowza' ),
    false,
    'hasnt_view(view, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'information_schema', 'tables', 'desc' ),
    false,
    'hasnt_view(sch, view, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test has_type().
SELECT * FROM check_test(
    has_type( 'sometype' ),
    true,
    'has_type(type)',
    'Type sometype should exist',
    ''
);
SELECT * FROM check_test(
    has_type( 'sometype', 'mydesc' ),
    true,
    'has_type(type, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_type( 'public'::name, 'sometype'::name ),
    true,
    'has_type(scheam, type)',
    'Type public.sometype should exist',
    ''
);
SELECT * FROM check_test(
    has_type( 'public', 'sometype', 'mydesc' ),
    true,
    'has_type(schema, type, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    has_type( '__foobarbaz__' ),
    false,
    'has_type(type)',
    'Type __foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_type( '__foobarbaz__', 'mydesc' ),
    false,
    'has_type(type, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_type( 'public'::name, '__foobarbaz__'::name ),
    false,
    'has_type(scheam, type)',
    'Type public.__foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_type( 'public', '__foobarbaz__', 'mydesc' ),
    false,
    'has_type(schema, type, desc)',
    'mydesc',
    ''
);

-- Make sure it works for domains.
SELECT * FROM check_test(
    has_type( 'us_postal_code' ),
    true,
    'has_type(domain)',
    'Type us_postal_code should exist',
    ''
);

/****************************************************************************/
-- Test hasnt_type().
SELECT * FROM check_test(
    hasnt_type( '__foobarbaz__' ),
    true,
    'hasnt_type(type)',
    'Type __foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_type( '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_type(type, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_type( 'public'::name, '__foobarbaz__'::name ),
    true,
    'hasnt_type(scheam, type)',
    'Type public.__foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_type( 'public', '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_type(schema, type, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    hasnt_type( 'sometype' ),
    false,
    'hasnt_type(type)',
    'Type sometype should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_type( 'sometype', 'mydesc' ),
    false,
    'hasnt_type(type, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_type( 'public'::name, 'sometype'::name ),
    false,
    'hasnt_type(scheam, type)',
    'Type public.sometype should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_type( 'public', 'sometype', 'mydesc' ),
    false,
    'hasnt_type(schema, type, desc)',
    'mydesc',
    ''
);

/****************************************************************************/
-- Test has_domain().
SELECT * FROM check_test(
    has_domain( 'us_postal_code' ),
    true,
    'has_domain(domain)',
    'Domain us_postal_code should exist',
    ''
);

SELECT * FROM check_test(
    has_domain( 'us_postal_code', 'mydesc' ),
    true,
    'has_domain(domain, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public'::name, 'us_postal_code'::name ),
    true,
    'has_domain(scheam, domain)',
    'Domain public.us_postal_code should exist',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public', 'us_postal_code', 'mydesc' ),
    true,
    'has_domain(schema, domain, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    has_domain( '__foobarbaz__' ),
    false,
    'has_domain(domain)',
    'Domain __foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_domain( '__foobarbaz__', 'mydesc' ),
    false,
    'has_domain(domain, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public'::name, '__foobarbaz__'::name ),
    false,
    'has_domain(scheam, domain)',
    'Domain public.__foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public', '__foobarbaz__', 'mydesc' ),
    false,
    'has_domain(schema, domain, desc)',
    'mydesc',
    ''
);

/****************************************************************************/
-- Test hasnt_domain().
SELECT * FROM check_test(
    hasnt_domain( '__foobarbaz__' ),
    true,
    'hasnt_domain(domain)',
    'Domain __foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_domain(domain, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( 'public'::name, '__foobarbaz__'::name ),
    true,
    'hasnt_domain(scheam, domain)',
    'Domain public.__foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( 'public', '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_domain(schema, domain, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    hasnt_domain( 'us_postal_code' ),
    false,
    'hasnt_domain(domain)',
    'Domain us_postal_code should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( 'us_postal_code', 'mydesc' ),
    false,
    'hasnt_domain(domain, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( 'public'::name, 'us_postal_code'::name ),
    false,
    'hasnt_domain(scheam, domain)',
    'Domain public.us_postal_code should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_domain( 'public', 'us_postal_code', 'mydesc' ),
    false,
    'hasnt_domain(schema, domain, desc)',
    'mydesc',
    ''
);

/****************************************************************************/
-- Test has_column().

SELECT * FROM check_test(
    has_column( '__SDFSDFD__', 'foo' ),
    false,
    'has_column(non-existent tab, col)',
    'Column __SDFSDFD__.foo should exist',
    ''
);

SELECT * FROM check_test(
    has_column( '__SDFSDFD__', 'bar', 'whatever' ),
    false,
    'has_column(non-existent tab, col, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_column( 'foo', '__SDFSDFD__', 'bar', 'desc' ),
    false,
    'has_column(non-existent sch, tab, col, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_column( 'sometab', 'id' ),
    true,
    'has_column(table, column)',
    'Column sometab.id should exist',
    ''
);

SELECT * FROM check_test(
    has_column( 'information_schema', 'tables', 'table_name', 'desc' ),
    true,
    'has_column(sch, tab, col, desc)',
    'desc',
    ''
);

-- Make sure it works with views.
SELECT * FROM check_test(
    has_column( 'pg_tables', 'schemaname' ),
    true,
    'has_column(view, column)',
    'Column pg_tables.schemaname should exist',
    ''
);

-- Make sure it works with composite types.
SELECT * FROM check_test(
    has_column( 'sometype', 'name' ),
    true,
    'has_column(type, column)',
    'Column sometype.name should exist',
    ''
);

/****************************************************************************/
-- Test hasnt_column().

SELECT * FROM check_test(
    hasnt_column( '__SDFSDFD__', 'foo' ),
    true,
    'hasnt_column(non-existent tab, col)',
    'Column __SDFSDFD__.foo should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_column( '__SDFSDFD__', 'bar', 'whatever' ),
    true,
    'hasnt_column(non-existent tab, col, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'foo', '__SDFSDFD__', 'bar', 'desc' ),
    true,
    'hasnt_column(non-existent sch, tab, col, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'sometab', 'id' ),
    false,
    'hasnt_column(table, column)',
    'Column sometab.id should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_column( 'information_schema', 'tables', 'table_name', 'desc' ),
    false,
    'hasnt_column(sch, tab, col, desc)',
    'desc',
    ''
);

-- Make sure it works with views.
SELECT * FROM check_test(
    hasnt_column( 'pg_tables', 'whatever' ),
    true,
    'hasnt_column(view, column)',
    'Column pg_tables.whatever should not exist',
    ''
);

-- Make sure it works with composite types.
SELECT * FROM check_test(
    hasnt_column( 'sometype', 'foobar' ),
    true,
    'hasnt_column(type, column)',
    'Column sometype.foobar should not exist',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
