\unset ECHO
\i test/setup.sql
-- \i sql/pgtap.sql

SELECT plan(1004);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2),
    "myInt" NUMERIC(8)
);

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

CREATE TYPE public.sometype AS (
    id    INT,
    name  TEXT
);

CREATE TYPE public."myType" AS (
    id INT,
    foo INT
);

CREATE DOMAIN public.us_postal_code AS TEXT CHECK(
    VALUE ~ '^[[:digit:]]{5}$' OR VALUE ~ '^[[:digit:]]{5}-[[:digit:]]{4}$'
);

CREATE DOMAIN public."myDomain" AS TEXT CHECK(TRUE);

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
    has_table( '__SDFSDFD__', 'lol'::name ),
    false,
    'has_table(non-existent schema, tab)',
    'Table "__SDFSDFD__".lol should exist',
    ''
);

SELECT * FROM check_test(
    has_table( '__SDFSDFD__', 'lol' ),
    false,
    'has_table(non-existent table, desc)',
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

-- But not partitions.
SELECT * FROM check_test(
    has_table( 'public', 'apart', 'have apart' ),
    true,
    'has_table(sch, part, desc)',
    'have apart',
    ''
);

SELECT * FROM check_test(
    has_table( 'apart', 'have apart' ),
    true,
    'has_table(part, desc)',
    'have apart',
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
    hasnt_table( '__SDFSDFD__', 'lol'::name ),
    true,
    'hasnt_table(non-existent schema, tab)',
    'Table "__SDFSDFD__".lol should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_table( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_table(non-existent table, desc)',
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

SELECT * FROM check_test(
    hasnt_table( 'apart', 'got apart' ),
    false,
    'hasnt_table(part, desc)',
    'got apart',
    ''
);

SELECT * FROM check_test(
    hasnt_table( 'public', 'apart', 'got apart' ),
    false,
    'hasnt_table(sch, part, desc)',
    'got apart',
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
    'has_view(sch, non-existent view, desc)',
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

SELECT * FROM check_test(
    has_view( 'information_schema', 'tables'::name ),
    true,
    'has_view(sch, view)',
    'View information_schema.tables should exist',
    ''
);

SELECT * FROM check_test(
    has_view( 'foo', '__SDFSDFD__'::name ),
    false,
    'has_view(sch, non-existent view, desc)',
    'View foo."__SDFSDFD__" should exist',
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
    'hasnt_view(sch, non-existent view, desc)',
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

SELECT * FROM check_test(
    hasnt_view( 'information_schema', 'tables'::name ),
    false,
    'hasnt_view(sch, view)',
    'View information_schema.tables should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_view( 'foo', '__SDFSDFD__'::name ),
    true,
    'hasnt_view(sch, non-existent view)',
    'View foo."__SDFSDFD__" should not exist',
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
    'has_sequence(sch, non-existent sequence, desc)',
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

SELECT * FROM check_test(
    has_sequence( 'public', 'someseq'::name ),
    true,
    'has_sequence(sch, sequence)',
    'Sequence public.someseq should exist'
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
    'hasnt_sequence(sch, non-existent sequence, desc)',
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
-- Test has_composite().

SELECT * FROM check_test(
    has_composite( '__SDFSDFD__' ),
    false,
    'has_composite(non-existent composite type)',
    'Composite type "__SDFSDFD__" should exist',
    ''
);

SELECT * FROM check_test(
    has_composite( '__SDFSDFD__', 'lol' ),
    false,
    'has_composite(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_composite( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_composite(sch, non-existent composite type, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_composite( 'sometype', 'lol' ),
    true,
    'has_composite(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_composite( 'public', 'sometype', 'desc' ),
    true,
    'has_composite(sch, tab, desc)',
    'desc',
    ''
);

-- It should ignore views and tables.
SELECT * FROM check_test(
    has_composite( 'pg_catalog', 'pg_composites', 'desc' ),
    false,
    'has_composite(sch, view, desc)',
    'desc',
    ''
);
SELECT * FROM check_test(
    has_composite( 'sometab', 'desc' ),
    false,
    'has_composite(type, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_composite().

SELECT * FROM check_test(
    hasnt_composite( '__SDFSDFD__' ),
    true,
    'hasnt_composite(non-existent composite type)',
    'Composite type "__SDFSDFD__" should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_composite( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_composite(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_composite( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_composite(sch, non-existent tab, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_composite( 'sometype', 'lol' ),
    false,
    'hasnt_composite(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_composite( 'public', 'sometype', 'desc' ),
    false,
    'hasnt_composite(sch, tab, desc)',
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

-- Try case-sensitive.
SELECT * FROM check_test(
    has_type( 'myType' ),
    true,
    'has_type(myType)',
    'Type "myType" should exist',
    ''
);
SELECT * FROM check_test(
    has_type( 'myType', 'mydesc' ),
    true,
    'has_type(myType, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_type( 'public'::name, 'myType'::name ),
    true,
    'has_type(scheam, myType)',
    'Type public."myType" should exist',
    ''
);
SELECT * FROM check_test(
    has_type( 'public', 'myType', 'mydesc' ),
    true,
    'has_type(schema, myType, desc)',
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

SELECT * FROM check_test(
    has_type( 'myDomain' ),
    true,
    'has_type(myDomain)',
    'Type "myDomain" should exist',
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

-- Try case-sensitive.
SELECT * FROM check_test(
    has_domain( 'myDomain' ),
    true,
    'has_domain(myDomain)',
    'Domain "myDomain" should exist',
    ''
);
SELECT * FROM check_test(
    has_domain( 'myDomain', 'mydesc' ),
    true,
    'has_domain(myDomain, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public'::name, 'myDomain'::name ),
    true,
    'has_domain(scheam, myDomain)',
    'Domain public."myDomain" should exist',
    ''
);
SELECT * FROM check_test(
    has_domain( 'public', 'myDomain', 'mydesc' ),
    true,
    'has_domain(schema, myDomain, desc)',
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

SELECT * FROM check_test(
    has_column( 'sometab', 'myInt' ),
    true,
    'has_column(table, camleCase column)',
    'Column sometab."myInt" should exist',
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
    has_cast( 'int4', 'BIGINT', 'pg_catalog', 'int8'::name),
    true,
    'has_cast( src, targ, schema, func )',
    'Cast (int4 AS "BIGINT") WITH FUNCTION pg_catalog.int8() should exist',
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
    has_cast( 'INT4', 'BIGINT', 'int8'::name),
    true,
    'has_cast( src, targ, func)',
    'Cast ("INT4" AS "BIGINT") WITH FUNCTION int8() should exist',
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
    has_cast( 'int4', 'BIGINT' ),
    true,
    'has_cast( src, targ )',
    'Cast (int4 AS "BIGINT") should exist',
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
    has_cast( 'INT4', 'BIGINT', 'foo', 'desc' ),
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
    hasnt_cast( 'int4', 'int8', 'pg_catalog', 'int8'::name),
    false,
    'hasnt_cast( src, targ, schema, func )',
    'Cast (int4 AS int8) WITH FUNCTION pg_catalog.int8() should not exist',
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
    hasnt_cast( 'INT4', 'INT8', 'int8'::name),
    false,
    'hasnt_cast( src, targ, func)',
    'Cast ("INT4" AS "INT8") WITH FUNCTION int8() should not exist',
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
    hasnt_cast( 'int4', 'int8' ),
    false,
    'hasnt_cast( src, targ )',
    'Cast (int4 AS int8) should not exist',
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
    hasnt_cast( 'INT4', 'INT8', 'foo', 'desc' ),
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
    cast_context_is( 'int4', 'int8', 'implicit' ),
    true,
    'cast_context_is( src, targ, context )',
    'Cast (int4 AS int8) context should be implicit',
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
    cast_context_is( 'INT4', 'INT8', 'IMPL', 'desc' ),
    true,
    'cast_context_is( src, targ, IMPL, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bigint', 'smallint', 'assignment', 'desc' ),
    true,
    'cast_context_is( src, targ, assignment, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'int4', 'int2', 'a', 'desc' ),
    true,
    'cast_context_is( src, targ, a, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bigint', 'smallint', 'ASS', 'desc' ),
    true,
    'cast_context_is( src, targ, ASS, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bit(128)', 'integer', 'explicit', 'desc' ),
    true,
    'cast_context_is( src, targ, explicit, desc )',
    'desc',
    ''
);

SELECT * FROM check_test(
    cast_context_is( 'bit', 'int4', 'e', 'desc' ),
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
    cast_context_is( 'integer', 'int8', 'ex', 'desc' ),
    false,
    'cast_context_is( src, targ, context, desc ) fail',
    'desc',
    '        have: implicit
        want: explicit'
);

SELECT * FROM check_test(
    cast_context_is( 'INT4', 'INT8', 'ex' ),
    false,
    'cast_context_is( src, targ, context ) fail',
    'Cast ("INT4" AS "INT8") context should be explicit',
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
  has_operator( 'integer', 'pg_catalog', '<=', 'int', 'bool', 'desc' ),
  true,
  'has_operator( left, schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'int4', 'pg_catalog', '<=', 'integer', 'boolean'::name ),
  true,
  'has_operator( left, schema, name, right, result )',
  'Operator pg_catalog.<=(int4,integer) RETURNS boolean should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'int', 'bool', 'desc' ),
  true,
  'has_operator( left, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'int4', 'boolean'::name ),
  true,
  'has_operator( left, name, right, result )',
  'Operator <=(integer,int4) RETURNS boolean should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'int', 'desc' ),
  true,
  'has_operator( left, name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'int4'::name ),
  true,
  'has_operator( left, name, right )',
  'Operator <=(integer,int4) should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'text', 'bool', 'desc' ),
  false,
  'has_operator( left, schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', 'pg_catalog', '<=', 'text', 'boolean'::name ),
  false,
  'has_operator( left, schema, name, right, result ) fail',
  'Operator pg_catalog.<=(integer,text) RETURNS boolean should exist',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text', 'bool', 'desc' ),
  false,
  'has_operator( left, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_operator( 'integer', '<=', 'text', 'boolean'::name ),
  false,
  'has_operator( left, name, right, result ) fail',
  'Operator <=(integer,text) RETURNS boolean should exist',
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
  'Operator <=(integer,text) should exist',
  ''
);

/****************************************************************************/
-- Test hasnt_operator().

SELECT * FROM check_test(
  hasnt_operator( 'integer', 'pg_catalog', '<=', 'integer', 'boolean', 'desc' ),
  false,
  'hasnt_operator( left, schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', 'pg_catalog', '<=', 'int', 'bool'::name ),
  false,
  'hasnt_operator( left, schema, name, right, result ) fail',
  'Operator pg_catalog.<=(integer,int) RETURNS bool should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'integer', 'boolean', 'desc' ),
  false,
  'hasnt_operator( left, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'int', 'bool'::name ),
  false,
  'hasnt_operator( left, name, right, result ) fail',
  'Operator <=(integer,int) RETURNS bool should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'integer', 'desc' ),
  false,
  'hasnt_operator( left, name, right, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'int'::name ),
  false,
  'hasnt_operator( left, name, right ) fail',
  'Operator <=(integer,int) should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', 'pg_catalog', '<=', 'text', 'boolean', 'desc' ),
  true,
  'hasnt_operator( left, schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', 'pg_catalog', '<=', 'text', 'bool'::name ),
  true,
  'hasnt_operator( left, schema, name, right, result )',
  'Operator pg_catalog.<=(integer,text) RETURNS bool should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'text', 'boolean', 'desc' ),
  true,
  'hasnt_operator( left, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'text', 'bool'::name ),
  true,
  'hasnt_operator( left, name, right, result )',
  'Operator <=(integer,text) RETURNS bool should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'text', 'desc' ),
  true,
  'hasnt_operator( left, name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_operator( 'integer', '<=', 'text'::name ),
  true,
  'hasnt_operator( left, name, right )',
  'Operator <=(integer,text) should not exist',
  ''
);

/****************************************************************************/
-- Test has_leftop().

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '+', 'bigint', 'int8', 'desc' ),
  true,
  'has_leftop( schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '+', 'bigint', 'bigint'::name ),
  true,
  'has_leftop( schema, name, right, result )',
  'Left operator pg_catalog.+(NONE,bigint) RETURNS bigint should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'bigint', 'int8', 'desc' ),
  true,
  'has_leftop( name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'bigint', 'int8'::name ),
  true,
  'has_leftop( name, right, result )',
  'Left operator +(NONE,bigint) RETURNS int8 should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'bigint', 'desc' ),
  true,
  'has_leftop( name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'int8' ),
  true,
  'has_leftop( name, right )',
  'Left operator +(NONE,int8) should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '+', 'text', 'numeric', 'desc' ),
  false,
  'has_leftop( schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( 'pg_catalog', '+', 'text', 'numeric'::name ),
  false,
  'has_leftop( schema, name, right, result ) fail',
  'Left operator pg_catalog.+(NONE,text) RETURNS numeric should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'text', 'inte', 'desc' ),
  false,
  'has_leftop( name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'text', 'int'::name ),
  false,
  'has_leftop( name, right, result ) fail',
  'Left operator +(NONE,text) RETURNS int should exist',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'text', 'desc' ),
  false,
  'has_leftop( name, right, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  has_leftop( '+', 'text' ),
  false,
  'has_leftop( name, right ) fail',
  'Left operator +(NONE,text) should exist',
  ''
);

/****************************************************************************/
-- Test hasnt_leftop().

SELECT * FROM check_test(
  hasnt_leftop( 'pg_catalog', '+', 'bigint', 'int8', 'desc' ),
  false,
  'hasnt_leftop( schema, name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( 'pg_catalog', '+', 'bigint', 'int8'::name ),
  false,
  'hasnt_leftop( schema, name, right, result ) fail',
  'Left operator pg_catalog.+(NONE,bigint) RETURNS int8 should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'bigint', 'bigint', 'desc' ),
  false,
  'hasnt_leftop( name, right, result, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'int8', 'int8'::name ),
  false,
  'hasnt_leftop( name, right, result ) fail',
  'Left operator +(NONE,int8) RETURNS int8 should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'bigint', 'desc' ),
  false,
  'hasnt_leftop( name, right, desc ) fail',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'int8' ),
  false,
  'hasnt_leftop( name, right ) fail',
  'Left operator +(NONE,int8) should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( 'pg_catalog', '+', 'text', 'bigint', 'desc' ),
  true,
  'hasnt_leftop( schema, name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( 'pg_catalog', '+', 'text', 'bigint'::name ),
  true,
  'hasnt_leftop( schema, name, right, result )',
  'Left operator pg_catalog.+(NONE,text) RETURNS bigint should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'text', 'bigint', 'desc' ),
  true,
  'hasnt_leftop( name, right, result, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'text', 'int8'::name ),
  true,
  'hasnt_leftop( name, right, result )',
  'Left operator +(NONE,text) RETURNS int8 should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'text', 'desc' ),
  true,
  'hasnt_leftop( name, right, desc )',
  'desc',
  ''
);

SELECT * FROM check_test(
  hasnt_leftop( '+', 'text' ),
  true,
  'hasnt_leftop( name, right )',
  'Left operator +(NONE,text) should not exist',
  ''
);

/****************************************************************************/
-- Test has_rightop().

CREATE FUNCTION test_has_rightop() RETURNS SETOF TEXT LANGUAGE plpgsql AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() < 140000 THEN
        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'bigint', 'pg_catalog', '!', 'numeric', 'desc' ),
            true,
            'has_rightop( left, schema, name, result, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'bigint', 'pg_catalog', '!', 'numeric'::name ),
            true,
            'has_rightop( left, schema, name, result )',
            'Right operator pg_catalog.!(bigint,NONE) RETURNS numeric should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'bigint', '!', 'numeric', 'desc' ),
            true,
            'has_rightop( left, name, result, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'bigint', '!', 'numeric'::name ),
            true,
            'has_rightop( left, name, result )',
            'Right operator !(bigint,NONE) RETURNS numeric should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'int8', '!', 'desc' ),
            true,
            'has_rightop( left, name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'int8', '!' ),
            true,
            'has_rightop( left, name )',
            'Right operator !(int8,NONE) should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', 'pg_catalog', '!', 'numeric', 'desc' ),
            false,
            'has_rightop( left, schema, name, result, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', 'pg_catalog', '!', 'numeric'::name ),
            false,
            'has_rightop( left, schema, name, result ) fail',
            'Right operator pg_catalog.!(text,NONE) RETURNS numeric should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', '!', 'numeric', 'desc' ),
            false,
            'has_rightop( left, name, result, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', '!', 'numeric'::name ),
            false,
            'has_rightop( left, name, result ) fail',
            'Right operator !(text,NONE) RETURNS numeric should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', '!', 'desc' ),
            false,
            'has_rightop( left, name, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_rightop( 'text', '!' ),
            false,
            'has_rightop( left, name ) fail',
            'Right operator !(text,NONE) should exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    ELSE
        -- PostgreSQL 14 dropped support for postfix operators, so mock the
        -- output for the tests to pass.
        FOR tap IN SELECT * FROM (VALUES
            ('has_rightop( left, schema, name, result, desc ) should pass'),
            ('has_rightop( left, schema, name, result, desc ) should have the proper description'),
            ('has_rightop( left, schema, name, result, desc ) should have the proper diagnostics'),
            ('has_rightop( left, schema, name, result ) should pass'),
            ('has_rightop( left, schema, name, result ) should have the proper description'),
            ('has_rightop( left, schema, name, result ) should have the proper diagnostics'),
            ('has_rightop( left, name, result, desc ) should pass'),
            ('has_rightop( left, name, result, desc ) should have the proper description'),
            ('has_rightop( left, name, result, desc ) should have the proper diagnostics'),
            ('has_rightop( left, name, result ) should pass'),
            ('has_rightop( left, name, result ) should have the proper description'),
            ('has_rightop( left, name, result ) should have the proper diagnostics'),
            ('has_rightop( left, name, desc ) should pass'),
            ('has_rightop( left, name, desc ) should have the proper description'),
            ('has_rightop( left, name, desc ) should have the proper diagnostics'),
            ('has_rightop( left, name ) should pass'),
            ('has_rightop( left, name ) should have the proper description'),
            ('has_rightop( left, name ) should have the proper diagnostics'),
            ('has_rightop( left, schema, name, result, desc ) fail should fail'),
            ('has_rightop( left, schema, name, result, desc ) fail should have the proper description'),
            ('has_rightop( left, schema, name, result, desc ) fail should have the proper diagnostics'),
            ('has_rightop( left, schema, name, result ) fail should fail'),
            ('has_rightop( left, schema, name, result ) fail should have the proper description'),
            ('has_rightop( left, schema, name, result ) fail should have the proper diagnostics'),
            ('has_rightop( left, name, result, desc ) fail should fail'),
            ('has_rightop( left, name, result, desc ) fail should have the proper description'),
            ('has_rightop( left, name, result, desc ) fail should have the proper diagnostics'),
            ('has_rightop( left, name, result ) fail should fail'),
            ('has_rightop( left, name, result ) fail should have the proper description'),
            ('has_rightop( left, name, result ) fail should have the proper diagnostics'),
            ('has_rightop( left, name, desc ) fail should fail'),
            ('has_rightop( left, name, desc ) fail should have the proper description'),
            ('has_rightop( left, name, desc ) fail should have the proper diagnostics'),
            ('has_rightop( left, name ) fail should fail'),
            ('has_rightop( left, name ) fail should have the proper description'),
            ('has_rightop( left, name ) fail should have the proper diagnostics')
        ) AS A(b) LOOP RETURN NEXT pass(tap.b); END LOOP;
    END IF;
END;
$$;
SELECT * FROM test_has_rightop();

/****************************************************************************/
-- Test hasnt_rightop().

CREATE FUNCTION test_hasnt_rightop() RETURNS SETOF TEXT LANGUAGE plpgsql AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() < 140000 THEN
        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'int8', 'pg_catalog', '!', 'numeric', 'desc' ),
            false,
            'hasnt_rightop( left, schema, name, result, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'bigint', 'pg_catalog', '!', 'numeric'::name ),
            false,
            'hasnt_rightop( left, schema, name, result ) fail',
            'Right operator pg_catalog.!(bigint,NONE) RETURNS numeric should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'int8', '!', 'numeric', 'desc' ),
            false,
            'hasnt_rightop( left, name, result, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'bigint', '!', 'numeric'::name ),
            false,
            'hasnt_rightop( left, name, result ) fail',
            'Right operator !(bigint,NONE) RETURNS numeric should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'int8', '!', 'desc' ),
            false,
            'hasnt_rightop( left, name, desc ) fail',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'bigint', '!' ),
            false,
            'hasnt_rightop( left, name ) fail',
            'Right operator !(bigint,NONE) should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', 'pg_catalog', '!', 'numeric', 'desc' ),
            true,
            'hasnt_rightop( left, schema, name, result, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', 'pg_catalog', '!', 'numeric'::name ),
            true,
            'hasnt_rightop( left, schema, name, result )',
            'Right operator pg_catalog.!(text,NONE) RETURNS numeric should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', '!', 'numeric', 'desc' ),
            true,
            'hasnt_rightop( left, name, result, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', '!', 'numeric'::name ),
            true,
            'hasnt_rightop( left, name, result )',
            'Right operator !(text,NONE) RETURNS numeric should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', '!', 'desc' ),
            true,
            'hasnt_rightop( left, name, desc )',
            'desc',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_rightop( 'text', '!' ),
            true,
            'hasnt_rightop( left, name )',
            'Right operator !(text,NONE) should not exist',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    ELSE
        -- PostgreSQL 14 dropped support for postfix operators, so mock the
        -- output for the tests to pass.
        FOR tap IN SELECT * FROM (VALUES
            ('hasnt_rightop( left, schema, name, result, desc ) fail should fail'),
            ('hasnt_rightop( left, schema, name, result, desc ) fail should have the proper description'),
            ('hasnt_rightop( left, schema, name, result, desc ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, schema, name, result ) fail should fail'),
            ('hasnt_rightop( left, schema, name, result ) fail should have the proper description'),
            ('hasnt_rightop( left, schema, name, result ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, name, result, desc ) fail should fail'),
            ('hasnt_rightop( left, name, result, desc ) fail should have the proper description'),
            ('hasnt_rightop( left, name, result, desc ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, name, result ) fail should fail'),
            ('hasnt_rightop( left, name, result ) fail should have the proper description'),
            ('hasnt_rightop( left, name, result ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, name, desc ) fail should fail'),
            ('hasnt_rightop( left, name, desc ) fail should have the proper description'),
            ('hasnt_rightop( left, name, desc ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, name ) fail should fail'),
            ('hasnt_rightop( left, name ) fail should have the proper description'),
            ('hasnt_rightop( left, name ) fail should have the proper diagnostics'),
            ('hasnt_rightop( left, schema, name, result, desc ) should pass'),
            ('hasnt_rightop( left, schema, name, result, desc ) should have the proper description'),
            ('hasnt_rightop( left, schema, name, result, desc ) should have the proper diagnostics'),
            ('hasnt_rightop( left, schema, name, result ) should pass'),
            ('hasnt_rightop( left, schema, name, result ) should have the proper description'),
            ('hasnt_rightop( left, schema, name, result ) should have the proper diagnostics'),
            ('hasnt_rightop( left, name, result, desc ) should pass'),
            ('hasnt_rightop( left, name, result, desc ) should have the proper description'),
            ('hasnt_rightop( left, name, result, desc ) should have the proper diagnostics'),
            ('hasnt_rightop( left, name, result ) should pass'),
            ('hasnt_rightop( left, name, result ) should have the proper description'),
            ('hasnt_rightop( left, name, result ) should have the proper diagnostics'),
            ('hasnt_rightop( left, name, desc ) should pass'),
            ('hasnt_rightop( left, name, desc ) should have the proper description'),
            ('hasnt_rightop( left, name, desc ) should have the proper diagnostics'),
            ('hasnt_rightop( left, name ) should pass'),
            ('hasnt_rightop( left, name ) should have the proper description'),
            ('hasnt_rightop( left, name ) should have the proper diagnostics')
        ) AS A(b) LOOP RETURN NEXT pass(tap.b); END LOOP;
    END IF;
END;
$$;
SELECT * FROM test_hasnt_rightop();

/****************************************************************************/
-- Test has_language() and hasnt_language().

SELECT * FROM check_test(
    has_language('plpgsql'),
    true,
    'has_language(language)',
    'Procedural language ' || quote_ident('plpgsql') || ' should exist',
    ''
);

SELECT * FROM check_test(
    has_language('plpgsql', 'whatever'),
    true,
    'has_language(language, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_language('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'has_language(nonexistent language)',
    'Procedural language aoijaoisjfaoidfjaisjdfosjf should exist',
    ''
);

SELECT * FROM check_test(
    has_language('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'has_language(nonexistent language, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_language('plpgsql'),
    false,
    'hasnt_language(language)',
    'Procedural language ' || quote_ident('plpgsql') || ' should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_language('plpgsql', 'whatever'),
    false,
    'hasnt_language(language, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_language('plomgwtf'),
    true,
    'hasnt_language(nonexistent language)',
    'Procedural language plomgwtf should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_language('plomgwtf', 'desc'),
    true,
    'hasnt_language(nonexistent language, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test language_is_trusted().
SELECT * FROM check_test(
    language_is_trusted('sql', 'whatever'),
    true,
    'language_is_trusted(language, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    language_is_trusted('sql'),
    true,
    'language_is_trusted(language)',
    'Procedural language sql should be trusted',
    ''
);

SELECT * FROM check_test(
    language_is_trusted('c', 'whatever'),
    false,
    'language_is_trusted(language, desc) fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    language_is_trusted('plomgwtf', 'whatever'),
    false,
    'language_is_trusted(language, desc) non-existent',
    'whatever',
    '    Procedural language plomgwtf does not exist'
);

/****************************************************************************/
-- Test has_opclass() and hasnt_opclass().

SELECT * FROM check_test(
  has_opclass( 'pg_catalog', 'int4_ops', 'whatever' ),
  true,
  'has_opclass( schema, name, desc )',
  'whatever',
  ''
);

SELECT * FROM check_test(
  has_opclass( 'pg_catalog', 'int4_ops'::name ),
  true,
  'has_opclass( schema, name )',
  'Operator class pg_catalog.int4_ops should exist',
  ''
);

SELECT * FROM check_test(
  has_opclass( 'int4_ops', 'whatever' ),
  true,
  'has_opclass( name, desc )',
  'whatever',
  ''
);

SELECT * FROM check_test(
  has_opclass( 'int4_ops' ),
  true,
  'has_opclass( name )',
  'Operator class int4_ops should exist',
  ''
);

SELECT * FROM check_test(
  has_opclass( 'pg_catalog', 'int4_opss', 'whatever' ),
  false,
  'has_opclass( schema, name, desc ) fail',
  'whatever',
  ''
);

SELECT * FROM check_test(
  has_opclass( 'int4_opss', 'whatever' ),
  false,
  'has_opclass( name, desc ) fail',
  'whatever',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'pg_catalog', 'int4_ops', 'whatever' ),
  false,
  'hasnt_opclass( schema, name, desc )',
  'whatever',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'pg_catalog', 'int4_ops'::name ),
  false,
  'hasnt_opclass( schema, name )',
  'Operator class pg_catalog.int4_ops should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'int4_ops', 'whatever' ),
  false,
  'hasnt_opclass( name, desc )',
  'whatever',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'int4_ops' ),
  false,
  'hasnt_opclass( name )',
  'Operator class int4_ops should not exist',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'pg_catalog', 'int4_opss', 'whatever' ),
  true,
  'hasnt_opclass( schema, name, desc ) fail',
  'whatever',
  ''
);

SELECT * FROM check_test(
  hasnt_opclass( 'int4_opss', 'whatever' ),
  true,
  'hasnt_opclass( name, desc ) fail',
  'whatever',
  ''
);

/****************************************************************************/
-- Test domain_type_is() and domain_type_isnt().
SELECT * FROM check_test(
    domain_type_is( 'public', 'us_postal_code', 'pg_catalog', 'text', 'whatever'),
    true,
    'domain_type_is(schema, domain, schema, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'us_postal_code', 'pg_catalog'::name, 'text'),
    true,
    'domain_type_is(schema, domain, schema, type)',
    'Domain public.us_postal_code should extend type pg_catalog.text',
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'us_postal_code', 'pg_catalog', 'int4', 'whatever'),
    false,
    'domain_type_is(schema, domain, schema, type, desc) fail',
    'whatever',
    '        have: pg_catalog.text
        want: pg_catalog.integer'
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'zip_code', 'pg_catalog', 'int', 'whatever'),
    false,
    'domain_type_is(schema, nondomain, schema, type, desc)',
    'whatever',
    '   Domain public.zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'integer', 'pg_catalog', 'int', 'whatever'),
    false,
    'domain_type_is(schema, type, schema, type, desc) fail',
    'whatever',
    '   Domain public.integer does not exist'
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'us_postal_code', 'text', 'whatever'),
    true,
    'domain_type_is(schema, domain, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'public'::name, 'us_postal_code', 'text'),
    true,
    'domain_type_is(schema, domain, type)',
    'Domain public.us_postal_code should extend type text', 
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'us_postal_code', 'int', 'whatever'),
    false,
    'domain_type_is(schema, domain, type, desc) fail',
    'whatever',
    '        have: text
        want: integer'
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'zip_code', 'int', 'whatever'),
    false,
    'domain_type_is(schema, nondomain, type, desc)',
    'whatever',
    '   Domain public.zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_is( 'public', 'integer', 'int', 'whatever'),
    false,
    'domain_type_is(schema, type, type, desc) fail',
    'whatever',
    '   Domain public.integer does not exist'
);

SELECT * FROM check_test(
    domain_type_is( 'us_postal_code', 'text', 'whatever'),
    true,
    'domain_type_is(domain, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'us_postal_code', 'text'),
    true,
    'domain_type_is(domain, type)',
    'Domain us_postal_code should extend type text', 
    ''
);

SELECT * FROM check_test(
    domain_type_is( 'us_postal_code', 'int', 'whatever'),
    false,
    'domain_type_is(domain, type, desc) fail',
    'whatever',
    '        have: text
        want: integer'
);

SELECT * FROM check_test(
    domain_type_is( 'zip_code', 'int', 'whatever'),
    false,
    'domain_type_is(nondomain, type, desc)',
    'whatever',
    '   Domain zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_is( 'integer', 'int', 'whatever'),
    false,
    'domain_type_is(type, type, desc) fail',
    'whatever',
    '   Domain integer does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'us_postal_code', 'public', 'int', 'whatever'),
    true,
    'domain_type_isnt(schema, domain, schema, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'us_postal_code', 'pg_catalog'::name, 'int4'),
    true,
    'domain_type_isnt(schema, domain, schema, type)',
    'Domain public.us_postal_code should not extend type pg_catalog.int4',
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'us_postal_code', 'pg_catalog', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, domain, schema, type, desc) fail',
    'whatever',
    '        have: pg_catalog.text
        want: anything else'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'zip_code', 'pg_catalog', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, nondomain, schema, type, desc)',
    'whatever',
    '   Domain public.zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'integer', 'pg_catalog', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, type, schema, type, desc)',
    'whatever',
    '   Domain public.integer does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'us_postal_code', 'integer', 'whatever'),
    true,
    'domain_type_isnt(schema, domain, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'public'::name, 'us_postal_code', 'int'),
    true,
    'domain_type_isnt(schema, domain, type)',
    'Domain public.us_postal_code should not extend type int', 
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'us_postal_code', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, domain, type, desc) fail',
    'whatever',
    '        have: text
        want: anything else'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'zip_code', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, nondomain, type, desc)',
    'whatever',
    '   Domain public.zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'public', 'integer', 'text', 'whatever'),
    false,
    'domain_type_isnt(schema, type, type, desc)',
    'whatever',
    '   Domain public.integer does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'us_postal_code', 'integer', 'whatever'),
    true,
    'domain_type_isnt(domain, type, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'us_postal_code', 'int'),
    true,
    'domain_type_isnt(domain, type)',
    'Domain us_postal_code should not extend type int', 
    ''
);

SELECT * FROM check_test(
    domain_type_isnt( 'us_postal_code', 'text', 'whatever'),
    false,
    'domain_type_isnt(domain, type, desc) fail',
    'whatever',
    '        have: text
        want: anything else'
);

SELECT * FROM check_test(
    domain_type_isnt( 'zip_code', 'text', 'whatever'),
    false,
    'domain_type_isnt(nondomain, type, desc)',
    'whatever',
    '   Domain zip_code does not exist'
);

SELECT * FROM check_test(
    domain_type_isnt( 'integer', 'text', 'whatever'),
    false,
    'domain_type_isnt(type, type, desc)',
    'whatever',
    '   Domain integer does not exist'
);

/****************************************************************************/
-- Test has_foreign_table() and hasnt_foreign_table().
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
            has_foreign_table( '__SDFSDFD__' ),
            false,
            'has_foreign_table(non-existent table)',
            'Foreign table "__SDFSDFD__" should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( '__SDFSDFD__', 'lol'::name ),
            false,
            'has_foreign_table(non-existent schema, tab)',
            'Foreign table "__SDFSDFD__".lol should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( '__SDFSDFD__', 'lol' ),
            false,
            'has_foreign_table(non-existent table, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'has_foreign_table(sch, non-existent table, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( 'my_fdw', 'lol' ),
            true,
            'has_foreign_table(tab, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( 'public', 'my_fdw', 'desc' ),
            true,
            'has_foreign_table(sch, tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        -- It should ignore views and types.
        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( 'pg_catalog', 'pg_foreign_tables', 'desc' ),
            false,
            'has_foreign_table(sch, view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_foreign_table( 'sometype', 'desc' ),
            false,
            'has_foreign_table(type, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( '__SDFSDFD__' ),
            true,
            'hasnt_foreign_table(non-existent table)',
            'Foreign table "__SDFSDFD__" should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( '__SDFSDFD__', 'lol'::name ),
            true,
            'hasnt_foreign_table(non-existent schema, tab)',
            'Foreign table "__SDFSDFD__".lol should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( '__SDFSDFD__', 'lol' ),
            true,
            'hasnt_foreign_table(non-existent table, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'hasnt_foreign_table(sch, non-existent tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( 'my_fdw', 'lol' ),
            false,
            'hasnt_foreign_table(tab, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_foreign_table( 'public', 'my_fdw', 'desc' ),
            false,
            'hasnt_foreign_table(sch, tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    ELSE
        -- Fake it with has_table().
        FOR tap IN SELECT * FROM check_test(
            has_table( '__SDFSDFD__' ),
            false,
            'has_foreign_table(non-existent table)',
            'Table "__SDFSDFD__" should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( '__SDFSDFD__', 'lol'::name ),
            false,
            'has_foreign_table(non-existent schema, tab)',
            'Table "__SDFSDFD__".lol should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( '__SDFSDFD__', 'lol' ),
            false,
            'has_foreign_table(non-existent table, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'has_foreign_table(sch, non-existent table, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( 'pg_type', 'lol' ),
            true,
            'has_foreign_table(tab, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( 'pg_catalog', 'pg_type', 'desc' ),
            true,
            'has_foreign_table(sch, tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        -- It should ignore views and types.
        FOR tap IN SELECT * FROM check_test(
            has_table( 'pg_catalog', 'pg_foreign_tables', 'desc' ),
            false,
            'has_foreign_table(sch, view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_table( 'sometype', 'desc' ),
            false,
            'has_foreign_table(type, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( '__SDFSDFD__' ),
            true,
            'hasnt_foreign_table(non-existent table)',
            'Table "__SDFSDFD__" should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( '__SDFSDFD__', 'lol'::name ),
            true,
            'hasnt_foreign_table(non-existent schema, tab)',
            'Table "__SDFSDFD__".lol should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( '__SDFSDFD__', 'lol' ),
            true,
            'hasnt_foreign_table(non-existent table, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'hasnt_foreign_table(sch, non-existent tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( 'pg_type', 'lol' ),
            false,
            'hasnt_foreign_table(tab, desc)',
            'lol',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_table( 'pg_catalog', 'pg_type', 'desc' ),
            false,
            'hasnt_foreign_table(sch, tab, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;

/****************************************************************************/
-- Test has_relation().

SELECT * FROM check_test(
    has_relation( '__SDFSDFD__' ),
    false,
    'has_relation(non-existent relation)',
    'Relation "__SDFSDFD__" should exist',
    ''
);

SELECT * FROM check_test(
    has_relation( '__SDFSDFD__', 'lol' ),
    false,
    'has_relation(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_relation( 'foo', '__SDFSDFD__', 'desc' ),
    false,
    'has_relation(sch, non-existent relation, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_relation( 'pg_type', 'lol' ),
    true,
    'has_relation(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    has_relation( 'pg_catalog', 'pg_type', 'desc' ),
    true,
    'has_relation(sch, tab, desc)',
    'desc',
    ''
);

-- It should not ignore views and types.
SELECT * FROM check_test(
    has_relation( 'pg_catalog', 'pg_type', 'desc' ),
    true,
    'has_relation(sch, view, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    has_relation( 'sometype', 'desc' ),
    true,
    'has_relation(type, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test hasnt_relation().

SELECT * FROM check_test(
    hasnt_relation( '__SDFSDFD__' ),
    true,
    'hasnt_relation(non-existent relation)',
    'Relation "__SDFSDFD__" should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_relation( '__SDFSDFD__', 'lol' ),
    true,
    'hasnt_relation(non-existent schema, tab)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_relation( 'foo', '__SDFSDFD__', 'desc' ),
    true,
    'hasnt_relation(sch, non-existent tab, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_relation( 'pg_type', 'lol' ),
    false,
    'hasnt_relation(tab, desc)',
    'lol',
    ''
);

SELECT * FROM check_test(
    hasnt_relation( 'pg_catalog', 'pg_type', 'desc' ),
    false,
    'hasnt_relation(sch, tab, desc)',
    'desc',
    ''
);

SELECT * FROM test_fdw();

/****************************************************************************/
-- Test has_materialized_view().

CREATE FUNCTION test_has_materialized_view() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 93000 THEN
        EXECUTE $E$
            CREATE MATERIALIZED VIEW public.mview AS SELECT * FROM public.sometab;
        $E$;

        FOR tap IN SELECT * FROM check_test(
            has_materialized_view( '__SDFSDFD__' ),
            false,
            'has_materialized_view(non-existent materialized_view)',
            'Materialized view "__SDFSDFD__" should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_materialized_view( '__SDFSDFD__', 'howdy' ),
            false,
            'has_materialized_view(non-existent materialized_view, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_materialized_view( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'has_materialized_view(sch, non-existent materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_materialized_view( 'mview', 'yowza' ),
            true,
            'has_materialized_view(materialized_view, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_materialized_view( 'public', 'mview', 'desc' ),
            true,
            'has_materialized_view(sch, materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

    ELSE
        FOR tap IN SELECT * FROM check_test(
            has_view( '__SDFSDFD__' ),
            false,
            'has_materialized_view(non-existent materialized_view)',
            'View "__SDFSDFD__" should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( '__SDFSDFD__', 'howdy' ),
            false,
            'has_materialized_view(non-existent materialized_view, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'has_materialized_view(sch, non-existent materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'pg_tables', 'yowza' ),
            true,
            'has_materialized_view(materialized_view, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'information_schema', 'tables', 'desc' ),
            true,
            'has_materialized_view(sch, materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ language PLPGSQL;

/****************************************************************************/
-- Test hasnt_materialized_view().
CREATE FUNCTION test_hasnt_materialized_views_are() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 93000 THEN

        FOR tap IN SELECT * FROM check_test(
            hasnt_materialized_view( '__SDFSDFD__' ),
            true,
            'hasnt_materialized_view(non-existent materialized_view)',
            'Materialized view "__SDFSDFD__" should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_materialized_view( '__SDFSDFD__', 'howdy' ),
            true,
            'hasnt_materialized_view(non-existent materialized_view, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_materialized_view( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'hasnt_materialized_view(sch, non-existent materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_materialized_view( 'mview', 'yowza' ),
            false,
            'hasnt_materialized_view(materialized_view, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_materialized_view( 'public', 'mview', 'desc' ),
            false,
            'hasnt_materialized_view(sch, materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    else
        FOR tap IN SELECT * FROM check_test(
            hasnt_view( '__SDFSDFD__' ),
            true,
            'hasnt_materialized_view(non-existent materialized_view)',
            'View "__SDFSDFD__" should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( '__SDFSDFD__', 'howdy' ),
            true,
            'hasnt_materialized_view(non-existent materialized_view, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'hasnt_materialized_view(sch, non-existent materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'pg_tables', 'yowza' ),
            false,
            'hasnt_materialized_view(materialized_view, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'information_schema', 'tables', 'desc' ),
            false,
            'hasnt_materialized_view(sch, materialized_view, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ language PLPGSQL;

SELECT * FROM test_has_materialized_view();
SELECT * FROm test_hasnt_materialized_views_are();

/****************************************************************************/
-- Test is_partitioned().
CREATE FUNCTION test_is_partitioned() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 100000 THEN
        FOR tap IN SELECT * FROM check_test(
            is_partitioned( '__SDFSDFD__' ),
            false,
            'is_partitioned(non-existent part)',
            'Table "__SDFSDFD__" should be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( '__SDFSDFD__', 'howdy' ),
            false,
            'is_partitioned(non-existent part, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'is_partitioned(sch, non-existent part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( 'public', 'apart', 'desc' ),
            true,
            'is_partitioned(sch, part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( 'public', 'apart'::name ),
            true,
            'is_partitioned(sch, part)',
            'Table public.apart should be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( 'apart', 'yowza' ),
            true,
            'is_partitioned(part, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            is_partitioned( 'apart' ),
            true,
            'is_partitioned(part)',
            'Table apart should be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

    ELSE
        FOR tap IN SELECT * FROM check_test(
            has_view( '__SDFSDFD__' ),
            false,
            'is_partitioned(non-existent part)',
            'View "__SDFSDFD__" should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( '__SDFSDFD__', 'howdy' ),
            false,
            'is_partitioned(non-existent part, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'foo', '__SDFSDFD__', 'desc' ),
            false,
            'is_partitioned(sch, non-existent part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'information_schema', 'tables', 'desc' ),
            true,
            'is_partitioned(sch, part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'information_schema', 'tables', 'desc' ),
            true,
            'is_partitioned(sch, part)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            has_view( 'information_schema', 'tables', 'desc' ),
            true,
            'is_partitioned(part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
        
        FOR tap IN SELECT * FROM check_test(
            has_view( 'pg_tables' ),
            true,
            'is_partitioned(part)',
            'View pg_tables should exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ language PLPGSQL;

/****************************************************************************/
-- Test isnt_partitioned().
CREATE FUNCTION test_isnt_partitioned() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 100000 THEN
        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( '__SDFSDFD__' ),
            true,
            'isnt_partitioned(non-existent part)',
            'Table "__SDFSDFD__" should not be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( '__SDFSDFD__', 'howdy' ),
            true,
            'isnt_partitioned(non-existent part, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'isnt_partitioned(sch, non-existent part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( 'public', 'apart', 'desc' ),
            false,
            'isnt_partitioned(sch, part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( 'public', 'apart'::name ),
            false,
            'isnt_partitioned(sch, part)',
            'Table public.apart should not be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( 'apart', 'yowza'::text ),
            false,
            'isnt_partitioned(part, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            isnt_partitioned( 'apart' ),
            false,
            'isnt_partitioned(part)',
            'Table apart should not be partitioned',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    else
        FOR tap IN SELECT * FROM check_test(
            hasnt_view( '__SDFSDFD__' ),
            true,
            'isnt_partitioned(non-existent part)',
            'View "__SDFSDFD__" should not exist',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( '__SDFSDFD__', 'howdy' ),
            true,
            'isnt_partitioned(non-existent part, desc)',
            'howdy',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'foo', '__SDFSDFD__', 'desc' ),
            true,
            'isnt_partitioned(sch, non-existent part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'information_schema', 'tables', 'desc' ),
            false,
            'isnt_partitioned(sch, part, desc)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'information_schema', 'tables', 'desc' ),
            false,
            'isnt_partitioned(sch, part)',
            'desc',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'pg_tables', 'yowza' ),
            false,
            'isnt_partitioned(part, desc)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            hasnt_view( 'pg_tables', 'yowza' ),
            false,
            'isnt_partitioned(part)',
            'yowza',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ language PLPGSQL;

SELECT * FROM test_is_partitioned();
SELECT * FROM test_isnt_partitioned();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
