\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(486);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE TYPE public.sometype AS (
    id    INT,
    name  TEXT
);

CREATE DOMAIN public.us_postal_code AS TEXT CHECK(
    VALUE ~ '^[[:digit:]]{5}$' OR VALUE ~ '^[[:digit:]]{5}-[[:digit:]]{4}$'
);

CREATE SEQUENCE public.someseq;

CREATE SCHEMA someschema;
RESET client_min_messages;

/****************************************************************************/
-- Test has_tablespace(). Can't really test with the location argument, though.
SELECT * FROM check_test(
    has_tablespace( '__SDFSDFD__' ),
    false,
    'has_tablespace(non-existent tablespace)',
    'Tablespace "__SDFSDFD__" should exist',
    ''
);
SELECT * FROM check_test(
    has_tablespace( '__SDFSDFD__', 'lol' ),
    false,
    'has_tablespace(non-existent tablespace, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_tablespace( 'pg_default' ),
    true,
    'has_tablespace(tablespace)',
    'Tablespace pg_default should exist',
    ''
);
SELECT * FROM check_test(
    has_tablespace( 'pg_default', 'lol' ),
    true,
    'has_tablespace(tablespace, desc)',
    'lol',
    ''
);

/****************************************************************************/
-- Test hasnt_tablespace().
SELECT * FROM check_test(
    hasnt_tablespace( '__SDFSDFD__' ),
    true,
    'hasnt_tablespace(non-existent tablespace)',
    'Tablespace "__SDFSDFD__" should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_tablespace( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_tablespace(non-existent tablespace, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_tablespace( 'pg_default' ),
    false,
    'hasnt_tablespace(pg_default)',
    'Tablespace pg_default should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_tablespace( 'pg_default', 'lol' ),
    false,
    'hasnt_tablespace(tablespace, desc)',
    'lol',
    ''
);

/****************************************************************************/
-- Test has_schema().
SELECT * FROM check_test(
    has_schema( '__SDFSDFD__' ),
    false,
    'has_schema(non-existent schema)',
    'Schema "__SDFSDFD__" should exist',
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
    'Schema "__SDFSDFD__" should not exist',
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
    'Table "__SDFSDFD__" should exist',
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
    'Table "__SDFSDFD__" should not exist',
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
    'View "__SDFSDFD__" should exist',
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
    'View "__SDFSDFD__" should not exist',
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
-- Test has_sequence().

SELECT * FROM check_test(
    has_sequence( '__SDFSDFD__' ),
    false,
    'has_sequence(non-existent sequence)',
    'Sequence "__SDFSDFD__" should exist',
    ''
);

SELECT * FROM check_test(
    has_sequence( '__SDFSDFD__', 'howdy' ),
    false,
    'has_sequence(non-existent sequence, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    has_sequence( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_sequence(sch, non-existtent sequence, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_sequence( 'someseq', 'yowza' ),
    true,
    'has_sequence(sequence, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    has_sequence( 'public', 'someseq', 'desc' ),
    true,
    'has_sequence(sch, sequence, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_sequence().

SELECT * FROM check_test(
    hasnt_sequence( '__SDFSDFD__' ),
    true,
    'hasnt_sequence(non-existent sequence)',
    'Sequence "__SDFSDFD__" should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_sequence( '__SDFSDFD__', 'howdy' ),
    true,
    'hasnt_sequence(non-existent sequence, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    hasnt_sequence( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_sequence(sch, non-existtent sequence, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_sequence( 'someseq', 'yowza' ),
    false,
    'hasnt_sequence(sequence, desc)',
    'yowza',
    ''
);

SELECT * FROM check_test(
    hasnt_sequence( 'public', 'someseq', 'desc' ),
    false,
    'hasnt_sequence(sch, sequence, desc)',
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
    'Column "__SDFSDFD__".foo should exist',
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
    'Column "__SDFSDFD__".foo should not exist',
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
-- Test has_cast().

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'pg_catalog', 'int8', 'desc' ),
    true,
    'has_cast( src, targ, schema, func, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'pg_catalog', 'int8'::name),
    true,
    'has_cast( src, targ, schema, func )',
    'Cast ("integer" AS "bigint") WITH FUNCTION pg_catalog.int8() should exist',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'int8', 'desc' ),
    true,
    'has_cast( src, targ, func, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'int8'::name),
    true,
    'has_cast( src, targ, func)',
    'Cast ("integer" AS "bigint") WITH FUNCTION int8() should exist',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'desc' ),
    true,
    'has_cast( src, targ, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint' ),
    true,
    'has_cast( src, targ )',
    'Cast ("integer" AS "bigint") should exist',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'pg_catalog', 'foo', 'desc' ),
    false,
    'has_cast( src, targ, schema, func, desc) fail',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'bigint', 'foo', 'desc' ),
    false,
    'has_cast( src, targ, func, desc ) fail',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_cast( 'integer', 'clue', 'desc' ),
    false,
    'has_cast( src, targ, desc ) fail',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_cast().

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'pg_catalog', 'int8', 'desc' ),
    false,
    'hasnt_cast( src, targ, schema, func, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'pg_catalog', 'int8'::name),
    false,
    'hasnt_cast( src, targ, schema, func )',
    'Cast ("integer" AS "bigint") WITH FUNCTION pg_catalog.int8() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'int8', 'desc' ),
    false,
    'hasnt_cast( src, targ, func, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'int8'::name),
    false,
    'hasnt_cast( src, targ, func)',
    'Cast ("integer" AS "bigint") WITH FUNCTION int8() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'desc' ),
    false,
    'hasnt_cast( src, targ, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint' ),
    false,
    'hasnt_cast( src, targ )',
    'Cast ("integer" AS "bigint") should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'pg_catalog', 'foo', 'desc' ),
    true,
    'hasnt_cast( src, targ, schema, func, desc) fail',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'bigint', 'foo', 'desc' ),
    true,
    'hasnt_cast( src, targ, func, desc ) fail',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_cast( 'integer', 'clue', 'desc' ),
    true,
    'hasnt_cast( src, targ, desc ) fail',
    'desc',
    ''
);

/****************************************************************************/
-- Test cast_has_context().

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'implicit', 'desc' ),
    true,
    'cast_context_is( src, targ, context, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'implicit' ),
    true,
    'cast_context_is( src, targ, context )',
    'Cast ("integer" AS "bigint") context should be implicit',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'i', 'desc' ),
    true,
    'cast_context_is( src, targ, i, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'IMPL', 'desc' ),
    true,
    'cast_context_is( src, targ, IMPL, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'cidr', 'text', 'assignment', 'desc' ),
    true,
    'cast_context_is( src, targ, assignment, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'cidr', 'text', 'a', 'desc' ),
    true,
    'cast_context_is( src, targ, a, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'cidr', 'text', 'ASS', 'desc' ),
    true,
    'cast_context_is( src, targ, ASS, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bit', 'integer', 'explicit', 'desc' ),
    true,
    'cast_context_is( src, targ, explicit, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bit', 'integer', 'e', 'desc' ),
    true,
    'cast_context_is( src, targ, e, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bit', 'integer', 'EX', 'desc' ),
    true,
    'cast_context_is( src, targ, EX, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'ex', 'desc' ),
    false,
    'cast_context_is( src, targ, context, desc ) fail',
    'desc',
    '        have: implicit
        want: explicit'
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bigint', 'ex' ),
    false,
    'cast_context_is( src, targ, context ) fail',
    'Cast ("integer" AS "bigint") context should be explicit',
    '        have: implicit
        want: explicit'
);

SELECT * FROM check_test(
    cast_context_is( 'integer', 'bogus', 'ex', 'desc' ),
    false,
    'cast_context_is( src, targ, context, desc ) noexist',
    'desc',
    '    Cast ("integer" AS bogus) does not exist'
);

/****************************************************************************/
-- Test has_operator().

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'integer', 'boolean', 'desc' ),
  true,
  'has_operator( left, schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'integer', 'boolean'::name ),
  true,
  'has_operator( left, schema, name, right, result )',
  'Operator ("integer" pg_catalog.<= "integer" = "boolean") should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'integer', 'boolean', 'desc' ),
  true,
  'has_operator( left, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'integer', 'boolean'::name ),
  true,
  'has_operator( left, name, right, result )',
  'Operator ("integer" <= "integer" = "boolean") should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'integer', 'desc' ),
  true,
  'has_operator( left, name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'integer'::name ),
  true,
  'has_operator( left, name, right )',
  'Operator ("integer" <= "integer") should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'text', 'boolean', 'desc' ),
  false,
  'has_operator( left, schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'text', 'boolean'::name ),
  false,
  'has_operator( left, schema, name, right, result ) fail',
  'Operator ("integer" pg_catalog.<= text = "boolean") should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text', 'boolean', 'desc' ),
  false,
  'has_operator( left, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text', 'boolean'::name ),
  false,
  'has_operator( left, name, right, result ) fail',
  'Operator ("integer" <= text = "boolean") should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text', 'desc' ),
  false,
  'has_operator( left, name, right, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text'::name ),
  false,
  'has_operator( left, name, right ) fail',
  'Operator ("integer" <= text) should exist',
  ''
);

/****************************************************************************/
-- Test has_leftop().

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '!!', 'bigint', 'numeric', 'desc' ),
  true,
  'has_leftop( schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '!!', 'bigint', 'numeric'::name ),
  true,
  'has_leftop( schema, name, right, result )',
  'Operator (pg_catalog.!! "bigint" = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'bigint', 'numeric', 'desc' ),
  true,
  'has_leftop( name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'bigint', 'numeric'::name ),
  true,
  'has_leftop( name, right, result )',
  'Operator (!! "bigint" = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'bigint', 'desc' ),
  true,
  'has_leftop( name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'bigint' ),
  true,
  'has_leftop( name, right )',
  'Operator (!! "bigint") should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '!!', 'text', 'numeric', 'desc' ),
  false,
  'has_leftop( schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '!!', 'text', 'numeric'::name ),
  false,
  'has_leftop( schema, name, right, result ) fail',
  'Operator (pg_catalog.!! text = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'text', 'numeric', 'desc' ),
  false,
  'has_leftop( name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'text', 'numeric'::name ),
  false,
  'has_leftop( name, right, result ) fail',
  'Operator (!! text = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'text', 'desc' ),
  false,
  'has_leftop( name, right, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '!!', 'text' ),
  false,
  'has_leftop( name, right ) fail',
  'Operator (!! text) should exist',
  ''
);

/****************************************************************************/
-- Test has_rightop().

SELECT * FROM check_test(
  has_rightop( 'bigint', 'pg_catalog', '!', 'numeric', 'desc' ),
  true,
  'has_rightop( left, schema, name, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'bigint', 'pg_catalog', '!', 'numeric'::name ),
  true,
  'has_rightop( left, schema, name, result )',
  'Operator ("bigint" pg_catalog.! = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'bigint', '!', 'numeric', 'desc' ),
  true,
  'has_rightop( left, name, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'bigint', '!', 'numeric'::name ),
  true,
  'has_rightop( left, name, result )',
  'Operator ("bigint" ! = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'bigint', '!', 'desc' ),
  true,
  'has_rightop( left, name, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'bigint', '!' ),
  true,
  'has_rightop( left, name )',
  'Operator ("bigint" !) should exist',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', 'pg_catalog', '!', 'numeric', 'desc' ),
  false,
  'has_rightop( left, schema, name, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', 'pg_catalog', '!', 'numeric'::name ),
  false,
  'has_rightop( left, schema, name, result ) fail',
  'Operator (text pg_catalog.! = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', '!', 'numeric', 'desc' ),
  false,
  'has_rightop( left, name, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', '!', 'numeric'::name ),
  false,
  'has_rightop( left, name, result ) fail',
  'Operator (text ! = "numeric") should exist',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', '!', 'desc' ),
  false,
  'has_rightop( left, name, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_rightop( 'text', '!' ),
  false,
  'has_rightop( left, name ) fail',
  'Operator (text !) should exist',
  ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
