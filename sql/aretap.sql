\unset ECHO
\i test_setup.sql

SELECT plan(90);
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

CREATE VIEW voo AS SELECT * FROM foo;
CREATE VIEW vou AS SELECT * FROM fou;

CREATE SEQUENCE public.someseq;
CREATE SEQUENCE public.sumeseq;

CREATE SCHEMA someschema;
RESET client_min_messages;

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
    'There should be the correct tables',
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
    'There should be the correct tables',
    '    These tables are missing:
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
    'There should be the correct tables',
    '    These are extra tables:
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
    'There should be the correct tables',
    '    These are extra tables:
        fo[ou]
        fo[ou]
    These tables are missing:
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
    'There should be the correct views',
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
    'There should be the correct views',
    '    These views are missing:
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
    'There should be the correct views',
    '    These are extra views:
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
    'There should be the correct views',
    '    These are extra views:
        vo[ou]
        vo[ou]
    These views are missing:
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
    'There should be the correct sequences',
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
    'There should be the correct sequences',
    '    These sequences are missing:
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
    'There should be the correct sequences',
    '    These are extra sequences:
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
    'There should be the correct sequences',
    '    These are extra sequences:
        s[ou]meseq
        s[ou]meseq
    These sequences are missing:
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
