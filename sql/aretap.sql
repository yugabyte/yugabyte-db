\unset ECHO
\i test_setup.sql

SELECT plan(179);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

CREATE TABLE public.fou(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE TABLE public.foo(
    id    INT NOT NULL PRIMARY KEY
);
CREATE TYPE public.sometype AS (
    id    INT,
    name  TEXT
);

CREATE INDEX idx_fou_id ON public.fou(id);
CREATE INDEX idx_fou_name ON public.fou(name);
CREATE INDEX idx_foo_id ON public.foo(id);

CREATE VIEW voo AS SELECT * FROM foo;
CREATE VIEW vou AS SELECT * FROM fou;

CREATE SEQUENCE public.someseq;
CREATE SEQUENCE public.sumeseq;

CREATE SCHEMA someschema;

CREATE FUNCTION someschema.yip() returns boolean as 'SELECT TRUE' language SQL;
CREATE FUNCTION someschema.yap() returns boolean as 'SELECT TRUE' language SQL;

RESET client_min_messages;

/****************************************************************************/
-- Test tablespaces_are().

CREATE FUNCTION ___myts(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT spcname
          FROM pg_catalog.pg_tablespace
         WHERE spcname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    tablespaces_are( ___myts(''), 'whatever' ),
    true,
    'tablespaces_are(schemas, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tablespaces_are( ___myts('') ),
    true,
    'tablespaces_are(schemas)',
    'There should be the correct tablespaces',
    ''
);

SELECT * FROM check_test(
    tablespaces_are( array_append(___myts(''), '__booya__'), 'whatever' ),
    false,
    'tablespaces_are(schemas, desc) missing',
    'whatever',
    '    Missing tablespaces:
        __booya__'
);

SELECT * FROM check_test(
    tablespaces_are( ___myts('pg_default'), 'whatever' ),
    false,
    'tablespaces_are(schemas, desc) extra',
    'whatever',
    '    Extra tablespaces:
        pg_default'
);

SELECT * FROM check_test(
    tablespaces_are( array_append(___myts('pg_default'), '__booya__'), 'whatever' ),
    false,
    'tablespaces_are(schemas, desc) extras and missing',
    'whatever',
    '    Extra tablespaces:
        pg_default
    Missing tablespaces:
        __booya__'
);

/****************************************************************************/
-- Test schemas_are().

CREATE FUNCTION ___mysch(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT nspname
          FROM pg_catalog.pg_namespace
         WHERE nspname NOT LIKE 'pg_%'
           AND nspname <> 'information_schema'
           AND nspname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    schemas_are( ___mysch(''), 'whatever' ),
    true,
    'schemas_are(schemas, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    schemas_are( ___mysch('') ),
    true,
    'schemas_are(schemas)',
    'There should be the correct schemas',
    ''
);

SELECT * FROM check_test(
    schemas_are( array_append(___mysch(''), '__howdy__'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) missing',
    'whatever',
    '    Missing schemas:
        __howdy__'
);

SELECT * FROM check_test(
    schemas_are( ___mysch('someschema'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) extras',
    'whatever',
    '    Extra schemas:
        someschema'
);

SELECT * FROM check_test(
    schemas_are( array_append(___mysch('someschema'), '__howdy__'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) missing and extras',
    'whatever',
    '    Extra schemas:
        someschema
    Missing schemas:
        __howdy__'
);

/****************************************************************************/
-- Test tables_are().

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo'], 'whatever' ),
    true,
    'tables_are(schema, tables, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo'] ),
    true,
    'tables_are(schema, tables)',
    'Schema public should have the correct tables',
    ''
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo'] ),
    true,
    'tables_are(tables)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    ''
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo'], 'whatever' ),
    true,
    'tables_are(tables, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo', 'bar'] ),
    false,
    'tables_are(schema, tables) missing',
    'Schema public should have the correct tables',
    '    Schema public is missing these tables:
        bar'
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo', 'bar'] ),
    false,
    'tables_are(tables) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' is missing these tables:
        bar'
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou'] ),
    false,
    'tables_are(schema, tables) extra',
    'Schema public should have the correct tables',
    '    Schema public has these extra tables:
        foo'
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou'] ),
    false,
    'tables_are(tables) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' has these extra tables:
        foo'
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'tables_are(schema, tables) extra and missing',
    'Schema public should have the correct tables',
    '    Schema public has these extra tables:
        fo[ou]
        fo[ou]
    Schema public is missing these tables:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    tables_are( ARRAY['bar', 'baz'] ),
    false,
    'tables_are(tables) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' has these extra tables:' || '
        fo[ou]
        fo[ou]
    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' is missing these tables:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test views_are().
SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo'], 'whatever' ),
    true,
    'views_are(schema, views, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo'] ),
    true,
    'views_are(schema, views)',
    'Schema public should have the correct views',
    ''
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo'] ),
    true,
    'views_are(views)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    ''
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo'], 'whatever' ),
    true,
    'views_are(views, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo', 'bar'] ),
    false,
    'views_are(schema, views) missing',
    'Schema public should have the correct views',
    '    Schema public is missing these views:
        bar'
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo', 'bar'] ),
    false,
    'views_are(views) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' is missing these views:
        bar'
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou'] ),
    false,
    'views_are(schema, views) extra',
    'Schema public should have the correct views',
    '    Schema public has these extra views:
        voo'
);

SELECT * FROM check_test(
    views_are( ARRAY['vou'] ),
    false,
    'views_are(views) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' has these extra views:
        voo'
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'views_are(schema, views) extra and missing',
    'Schema public should have the correct views',
    '    Schema public has these extra views:
        vo[ou]
        vo[ou]
    Schema public is missing these views:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    views_are( ARRAY['bar', 'baz'] ),
    false,
    'views_are(views) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' has these extra views:' || '
        vo[ou]
        vo[ou]
    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' is missing these views:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test sequences_are().
SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq'], 'whatever' ),
    true,
    'sequences_are(schema, sequences, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq'] ),
    true,
    'sequences_are(schema, sequences)',
    'Schema public should have the correct sequences',
    ''
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq'] ),
    true,
    'sequences_are(sequences)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    ''
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq'], 'whatever' ),
    true,
    'sequences_are(sequences, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq', 'bar'] ),
    false,
    'sequences_are(schema, sequences) missing',
    'Schema public should have the correct sequences',
    '    Schema public is missing these sequences:
        bar'
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq', 'bar'] ),
    false,
    'sequences_are(sequences) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' is missing these sequences:
        bar'
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq'] ),
    false,
    'sequences_are(schema, sequences) extra',
    'Schema public should have the correct sequences',
    '    Schema public has these extra sequences:
        someseq'
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq'] ),
    false,
    'sequences_are(sequences) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' has these extra sequences:
        someseq'
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'sequences_are(schema, sequences) extra and missing',
    'Schema public should have the correct sequences',
    '    Schema public has these extra sequences:
        s[ou]meseq
        s[ou]meseq
    Schema public is missing these sequences:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    sequences_are( ARRAY['bar', 'baz'] ),
    false,
    'sequences_are(sequences) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' has these extra sequences:' || '
        s[ou]meseq
        s[ou]meseq
    Search path ' || replace(pg_catalog.current_setting('search_path'), '$', E'\\$') || ' is missing these sequences:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test functions_are().

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap'], 'whatever' ),
    true,
    'functions_are(schema, functions, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap'] ),
    true,
    'functions_are(schema, functions)',
    'Schema someschema should have the correct functions'
    ''
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap', 'yop'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + missing',
    'whatever',
    '    Schema someschema is missing these functions:
        yop'
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + extra',
    'whatever',
    '    Schema someschema has these extra functions:
        yap'
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yap', 'yop'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + extra & missing',
    'whatever',
    '    Schema someschema has these extra functions:
        yip
    Schema someschema is missing these functions:
        yop'
);

CREATE FUNCTION ___myfunk(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT p.proname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_proc p ON n.oid = p.pronamespace
         WHERE pg_catalog.pg_function_is_visible(p.oid)
           AND n.nspname <> 'pg_catalog'
           AND p.proname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    functions_are( ___myfunk(''), 'whatever' ),
    true,
    'functions_are(functions, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    functions_are( ___myfunk('') ),
    true,
    'functions_are(functions)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct functions',
    ''
);

SELECT * FROM check_test(
    functions_are( array_append(___myfunk(''), '__booyah__'), 'whatever' ),
    false,
    'functions_are(functions, desc) + missing',
    'whatever',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' is missing these functions:
        __booyah__'
);

SELECT * FROM check_test(
    functions_are( ___myfunk('check_test'), 'whatever' ),
    false,
    'functions_are(functions, desc) + extra',
    'whatever',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' has these extra functions:
        check_test'
);

SELECT * FROM check_test(
    functions_are( array_append(___myfunk('check_test'), '__booyah__'), 'whatever' ),
    false,
    'functions_are(functions, desc) + extra & missing',
    'whatever',
    '    Search path ' || pg_catalog.current_setting('search_path')  || ' has these extra functions:
        check_test
    Search path ' || pg_catalog.current_setting('search_path')  || ' is missing these functions:
        __booyah__'
);

/****************************************************************************/
-- Test indexes_are().
SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'], 'whatever' ),
    true,
    'indexes_are(schema, table, indexes, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'] ),
    true,
    'indexes_are(schema, table, indexes)',
    'Table public.fou should have the correct indexes',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name'] ),
    false,
    'indexes_are(schema, table, indexes) + extra',
    'Table public.fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey'
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey', 'howdy'] ),
    false,
    'indexes_are(schema, table, indexes) + missing',
    'Table public.fou should have the correct indexes',
    '    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'howdy'] ),
    false,
    'indexes_are(schema, table, indexes) + extra & missing',
    'Table public.fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey
    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'], 'whatever' ),
    true,
    'indexes_are(table, indexes, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'] ),
    true,
    'indexes_are(table, indexes)',
    'Table fou should have the correct indexes',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name'] ),
    false,
    'indexes_are(table, indexes) + extra',
    'Table fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey', 'howdy'] ),
    false,
    'indexes_are(table, indexes) + missing',
    'Table fou should have the correct indexes',
    '    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'howdy'] ),
    false,
    'indexes_are(table, indexes) + extra & missing',
    'Table fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey
    Missing indexes:
        howdy'
);

SELECT indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'howdy'] );

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
