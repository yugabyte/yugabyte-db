\unset ECHO
\i test/setup.sql

SELECT plan(545);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

CREATE TABLE names (
    id    SERIAL NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
);

CREATE TABLE someat (
    id     SERIAL  PRIMARY KEY,
    ts     timestamp DEFAULT NOW(),
    active BOOLEAN DEFAULT TRUE
);

RESET client_min_messages;

-- Top 100 boy an 100 girl names in 2005. https://www.ssa.gov/OACT/babynames/
INSERT INTO names (name) VALUES ('Jacob');
INSERT INTO names (name) VALUES ('Emily');
INSERT INTO names (name) VALUES ('Michael');
INSERT INTO names (name) VALUES ('Emma');
INSERT INTO names (name) VALUES ('Joshua');
INSERT INTO names (name) VALUES ('Madison');
INSERT INTO names (name) VALUES ('Matthew');
INSERT INTO names (name) VALUES ('Abigail');
INSERT INTO names (name) VALUES ('Ethan');
INSERT INTO names (name) VALUES ('Olivia');
INSERT INTO names (name) VALUES ('Andrew');
INSERT INTO names (name) VALUES ('Isabella');
INSERT INTO names (name) VALUES ('Daniel');
INSERT INTO names (name) VALUES ('Hannah');
INSERT INTO names (name) VALUES ('Anthony');
INSERT INTO names (name) VALUES ('Samantha');
INSERT INTO names (name) VALUES ('Christopher');
INSERT INTO names (name) VALUES ('Ava');
INSERT INTO names (name) VALUES ('Joseph');
INSERT INTO names (name) VALUES ('Ashley');
INSERT INTO names (name) VALUES ('William');
INSERT INTO names (name) VALUES ('Elizabeth');
INSERT INTO names (name) VALUES ('Alexander');
INSERT INTO names (name) VALUES ('Sophia');
INSERT INTO names (name) VALUES ('David');
INSERT INTO names (name) VALUES ('Alexis');
INSERT INTO names (name) VALUES ('Ryan');
INSERT INTO names (name) VALUES ('Grace');
INSERT INTO names (name) VALUES ('Nicholas');
INSERT INTO names (name) VALUES ('Sarah');
INSERT INTO names (name) VALUES ('Tyler');
INSERT INTO names (name) VALUES ('Alyssa');
INSERT INTO names (name) VALUES ('James');
INSERT INTO names (name) VALUES ('Mia');
INSERT INTO names (name) VALUES ('John');
INSERT INTO names (name) VALUES ('Natalie');
INSERT INTO names (name) VALUES ('Jonathan');
INSERT INTO names (name) VALUES ('Chloe');
INSERT INTO names (name) VALUES ('Nathan');
INSERT INTO names (name) VALUES ('Brianna');
INSERT INTO names (name) VALUES ('Samuel');
INSERT INTO names (name) VALUES ('Lauren');
INSERT INTO names (name) VALUES ('Christian');
INSERT INTO names (name) VALUES ('Anna');
INSERT INTO names (name) VALUES ('Noah');
INSERT INTO names (name) VALUES ('Ella');
INSERT INTO names (name) VALUES ('Dylan');
INSERT INTO names (name) VALUES ('Taylor');
INSERT INTO names (name) VALUES ('Benjamin');
INSERT INTO names (name) VALUES ('Kayla');
INSERT INTO names (name) VALUES ('Logan');
INSERT INTO names (name) VALUES ('Hailey');
INSERT INTO names (name) VALUES ('Brandon');
INSERT INTO names (name) VALUES ('Jessica');
INSERT INTO names (name) VALUES ('Gabriel');
INSERT INTO names (name) VALUES ('Victoria');
INSERT INTO names (name) VALUES ('Zachary');
INSERT INTO names (name) VALUES ('Jasmine');
INSERT INTO names (name) VALUES ('Jose');
INSERT INTO names (name) VALUES ('Sydney');
INSERT INTO names (name) VALUES ('Elijah');
INSERT INTO names (name) VALUES ('Julia');
INSERT INTO names (name) VALUES ('Angel');
INSERT INTO names (name) VALUES ('Destiny');
INSERT INTO names (name) VALUES ('Kevin');
INSERT INTO names (name) VALUES ('Morgan');
INSERT INTO names (name) VALUES ('Jack');
INSERT INTO names (name) VALUES ('Kaitlyn');
INSERT INTO names (name) VALUES ('Caleb');
INSERT INTO names (name) VALUES ('Savannah');
INSERT INTO names (name) VALUES ('Justin');
INSERT INTO names (name) VALUES ('Katherine');
INSERT INTO names (name) VALUES ('Robert');
INSERT INTO names (name) VALUES ('Alexandra');
INSERT INTO names (name) VALUES ('Austin');
INSERT INTO names (name) VALUES ('Rachel');
INSERT INTO names (name) VALUES ('Evan');
INSERT INTO names (name) VALUES ('Lily');
INSERT INTO names (name) VALUES ('Thomas');
INSERT INTO names (name) VALUES ('Kaylee');
INSERT INTO names (name) VALUES ('Luke');
INSERT INTO names (name) VALUES ('Megan');
INSERT INTO names (name) VALUES ('Mason');
INSERT INTO names (name) VALUES ('Jennifer');
INSERT INTO names (name) VALUES ('Aidan');
INSERT INTO names (name) VALUES ('Angelina');
INSERT INTO names (name) VALUES ('Jackson');
INSERT INTO names (name) VALUES ('Makayla');
INSERT INTO names (name) VALUES ('Isaiah');
INSERT INTO names (name) VALUES ('Allison');
INSERT INTO names (name) VALUES ('Jordan');
INSERT INTO names (name) VALUES ('Maria');
INSERT INTO names (name) VALUES ('Gavin');
INSERT INTO names (name) VALUES ('Brooke');
INSERT INTO names (name) VALUES ('Connor');
INSERT INTO names (name) VALUES ('Trinity');
INSERT INTO names (name) VALUES ('Isaac');
INSERT INTO names (name) VALUES ('Faith');
INSERT INTO names (name) VALUES ('Aiden');
INSERT INTO names (name) VALUES ('Lillian');
INSERT INTO names (name) VALUES ('Jason');
INSERT INTO names (name) VALUES ('Mackenzie');
INSERT INTO names (name) VALUES ('Cameron');
INSERT INTO names (name) VALUES ('Sofia');
INSERT INTO names (name) VALUES ('Hunter');
INSERT INTO names (name) VALUES ('Riley');
INSERT INTO names (name) VALUES ('Jayden');
INSERT INTO names (name) VALUES ('Haley');
INSERT INTO names (name) VALUES ('Juan');
INSERT INTO names (name) VALUES ('Gabrielle');
INSERT INTO names (name) VALUES ('Charles');
INSERT INTO names (name) VALUES ('Nicole');
INSERT INTO names (name) VALUES ('Aaron');
INSERT INTO names (name) VALUES ('Kylie');
INSERT INTO names (name) VALUES ('Lucas');
INSERT INTO names (name) VALUES ('Zoe');
INSERT INTO names (name) VALUES ('Luis');
INSERT INTO names (name) VALUES ('Katelyn');
INSERT INTO names (name) VALUES ('Owen');
INSERT INTO names (name) VALUES ('Paige');
INSERT INTO names (name) VALUES ('Landon');
INSERT INTO names (name) VALUES ('Gabriella');
INSERT INTO names (name) VALUES ('Diego');
INSERT INTO names (name) VALUES ('Jenna');
INSERT INTO names (name) VALUES ('Brian');
INSERT INTO names (name) VALUES ('Kimberly');
INSERT INTO names (name) VALUES ('Adam');
INSERT INTO names (name) VALUES ('Stephanie');
INSERT INTO names (name) VALUES ('Adrian');
INSERT INTO names (name) VALUES ('Andrea');
INSERT INTO names (name) VALUES ('Eric');
INSERT INTO names (name) VALUES ('Alexa');
INSERT INTO names (name) VALUES ('Kyle');
INSERT INTO names (name) VALUES ('Avery');
INSERT INTO names (name) VALUES ('Ian');
INSERT INTO names (name) VALUES ('Leah');
INSERT INTO names (name) VALUES ('Nathaniel');
INSERT INTO names (name) VALUES ('Nevaeh');
INSERT INTO names (name) VALUES ('Carlos');
INSERT INTO names (name) VALUES ('Madeline');
INSERT INTO names (name) VALUES ('Alex');
INSERT INTO names (name) VALUES ('Evelyn');
INSERT INTO names (name) VALUES ('Bryan');
INSERT INTO names (name) VALUES ('Mary');
INSERT INTO names (name) VALUES ('Jesus');
INSERT INTO names (name) VALUES ('Maya');
INSERT INTO names (name) VALUES ('Julian');
INSERT INTO names (name) VALUES ('Michelle');
INSERT INTO names (name) VALUES ('Sean');
INSERT INTO names (name) VALUES ('Sara');
INSERT INTO names (name) VALUES ('Hayden');
INSERT INTO names (name) VALUES ('Jada');
INSERT INTO names (name) VALUES ('Carter');
INSERT INTO names (name) VALUES ('Audrey');
INSERT INTO names (name) VALUES ('Jeremiah');
INSERT INTO names (name) VALUES ('Brooklyn');
INSERT INTO names (name) VALUES ('Cole');
INSERT INTO names (name) VALUES ('Vanessa');
INSERT INTO names (name) VALUES ('Brayden');
INSERT INTO names (name) VALUES ('Amanda');
INSERT INTO names (name) VALUES ('Wyatt');
INSERT INTO names (name) VALUES ('Rebecca');
INSERT INTO names (name) VALUES ('Chase');
INSERT INTO names (name) VALUES ('Caroline');
INSERT INTO names (name) VALUES ('Steven');
INSERT INTO names (name) VALUES ('Ariana');
INSERT INTO names (name) VALUES ('Timothy');
INSERT INTO names (name) VALUES ('Amelia');
INSERT INTO names (name) VALUES ('Dominic');
INSERT INTO names (name) VALUES ('Mariah');
INSERT INTO names (name) VALUES ('Sebastian');
INSERT INTO names (name) VALUES ('Jordan');
INSERT INTO names (name) VALUES ('Xavier');
INSERT INTO names (name) VALUES ('Jocelyn');
INSERT INTO names (name) VALUES ('Jaden');
INSERT INTO names (name) VALUES ('Arianna');
INSERT INTO names (name) VALUES ('Jesse');
INSERT INTO names (name) VALUES ('Isabel');
INSERT INTO names (name) VALUES ('Seth');
INSERT INTO names (name) VALUES ('Marissa');
INSERT INTO names (name) VALUES ('Devin');
INSERT INTO names (name) VALUES ('Autumn');
INSERT INTO names (name) VALUES ('Antonio');
INSERT INTO names (name) VALUES ('Melanie');
INSERT INTO names (name) VALUES ('Miguel');
INSERT INTO names (name) VALUES ('Aaliyah');
INSERT INTO names (name) VALUES ('Richard');
INSERT INTO names (name) VALUES ('Gracie');
INSERT INTO names (name) VALUES ('Colin');
INSERT INTO names (name) VALUES ('Claire');
INSERT INTO names (name) VALUES ('Cody');
INSERT INTO names (name) VALUES ('Isabelle');
INSERT INTO names (name) VALUES ('Alejandro');
INSERT INTO names (name) VALUES ('Molly');
INSERT INTO names (name) VALUES ('Caden');
INSERT INTO names (name) VALUES ('Mya');
INSERT INTO names (name) VALUES ('Blake');
INSERT INTO names (name) VALUES ('Diana');
INSERT INTO names (name) VALUES ('Kaden');
INSERT INTO names (name) VALUES ('Katie');

CREATE TABLE annames AS
SELECT id, name FROM names WHERE name like 'An%';

-- We'll use these prepared statements.
PREPARE anames AS SELECT id, name FROM names WHERE name like 'An%';
CREATE TABLE toexpect (id int, name text);
INSERT INTO toexpect (id, name) VALUES(11, 'Andrew');
INSERT INTO toexpect (id, name) VALUES(44, 'Anna');
INSERT INTO toexpect (id, name) VALUES(15, 'Anthony');
INSERT INTO toexpect (id, name) VALUES(183, 'Antonio');
INSERT INTO toexpect (id, name) VALUES(86, 'Angelina');
INSERT INTO toexpect (id, name) VALUES(130, 'Andrea');
INSERT INTO toexpect (id, name) VALUES(63, 'Angel');
PREPARE expect AS SELECT id, name FROM toexpect;

/****************************************************************************/
-- First, test _temptable.

SELECT is(
    _temptable('SELECT * FROM names', '__foonames__'),
     '__foonames__',
     'Should create temp table with simple query'
);
SELECT has_table('__foonames__' );

SELECT is(
    _temptable( 'anames', '__somenames__' ),
    '__somenames__',
    'Should create a temp table for a prepared statement'
);
SELECT has_table('__somenames__' );

PREPARE "something cool" AS SELECT 1 AS a, 2 AS b;
SELECT is(
    _temptable( '"something cool"', '__spacenames__' ),
    '__spacenames__',
    'Should create a temp table for a prepared statement with space'
);
SELECT has_table('__somenames__' );
SELECT has_table('__spacenames__' );

/****************************************************************************/
-- Now test set_eq().

SELECT * FROM check_test(
    set_eq( 'anames', 'expect', 'whatever' ),
    true,
    'set_eq(prepared, prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_eq( 'anames', 'expect' ),
    true,
    'set_eq(prepared, prepared)',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    set_eq( 'EXECUTE anames', 'EXECUTE expect' ),
    true,
    'set_eq(execute, execute, desc)',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM annames'
    ),
    true,
    'set_eq(select, select)',
    '',
    ''
);

-- Make sure that dupes are disregarded.
SELECT * FROM check_test(
    set_eq(
        'SELECT 1 AS a, ''Anna''::text AS b',
        'SELECT 1 AS a, ''Anna''::text AS b UNION ALL SELECT 1, ''Anna'''
    ),
    true,
    'set_eq(values, dupe values)',
    '',
    ''
);

-- Try some failures.
SELECT * FROM check_test(
    set_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    false,
    'set_eq(prepared, select) fail extra',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')'
    ),
    false,
    'set_eq(prepared, select) fail extras',
    '',
    '    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'set_eq(select, prepared) fail missing',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'set_eq(select, prepared) fail missings',
    '',
    '    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob)'' AND name <> ''Anna''',
        'SELECT id, name FROM annames'
    ),
    false,
    'set_eq(select, select) fail extra & missing',
    '',
    '    Extra records:
        (1,Jacob)
    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob|Jacks)'' AND name NOT IN (''Anna'', ''Angelina'')',
        'SELECT id, name FROM annames'
    ),
    false,
    'set_eq(select, select) fail extras & missings',
    '',
    '    Extra records:
        [(](1,Jacob|87,Jackson)[)]
        [(](1,Jacob|87,Jackson)[)]
    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_eq(
        'SELECT 1 AS a, ''foo''::text AS b UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 AS b UNION ALL SELECT ''bar'', 2' ),
    false,
    'set_eq(values, values) fail mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_eq(
        'SELECT 1 AS a UNION ALL SELECT 2 AS b',
        'SELECT ''foo''::text AS a, 1 AS b UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'set_eq(values, values) fail column count',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

-- Handle failure with column mismatch with a column type in a schema not in
-- the search path. This is a regression.
CREATE SCHEMA __myfoo;
CREATE DOMAIN __myfoo.text AS TEXT CHECK(TRUE);
CREATE TABLE __yowza(
    foo text,
    bar __myfoo.text,
    baz integer
);
INSERT INTO __yowza VALUES ('abc', 'xyz', 1);
INSERT INTO __yowza VALUES ('def', 'utf', 2);

SELECT * FROM check_test(
    set_eq(
        'SELECT foo, bar from __yowza',
        'SELECT foo, bar, baz from __yowza'
    ),
    false,
    'set_eq(sql, sql) fail type schema visibility',
    '',
    '    Columns differ between queries:
        have: (text,__myfoo.text)
        want: (text,__myfoo.text,integer)'
);

/****************************************************************************/
-- Now test bag_eq().

SELECT * FROM check_test(
    bag_eq( 'anames', 'expect', 'whatever' ),
    true,
    'bag_eq(prepared, prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_eq( 'anames', 'expect' ),
    true,
    'bag_eq(prepared, prepared)',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    bag_eq( 'EXECUTE anames', 'EXECUTE expect' ),
    true,
    'bag_eq(execute, execute)',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM annames'
    ),
    true,
    'bag_eq(select, select)',
    '',
    ''
);

-- Compare with dupes.
SELECT * FROM check_test(
    bag_eq(
        'SELECT 1 AS a, ''Anna''::text AS b UNION ALL SELECT 86, ''Angelina'' UNION ALL SELECT 1, ''Anna''',
        'SELECT 1 AS a, ''Anna''::text AS b UNION ALL SELECT 1, ''Anna'' UNION ALL SELECT 86, ''Angelina'''
    ),
    true,
    'bag_eq(dupe values, dupe values)',
    '',
    ''
);

-- And now some failures.
SELECT * FROM check_test(
    bag_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    false,
    'bag_eq(prepared, select) fail extra',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')'
    ),
    false,
    'bag_eq(prepared, select) fail extras',
    '',
    '    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'bag_eq(select, prepared) fail missing',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'bag_eq(select, prepared) fail missings',
    '',
    '    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob)'' AND name <> ''Anna''',
        'SELECT id, name FROM annames'
    ),
    false,
    'bag_eq(select, select) fail extra & missing',
    '',
    '    Extra records:
        (1,Jacob)
    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob|Jacks)'' AND name NOT IN (''Anna'', ''Angelina'')',
        'SELECT id, name FROM annames'
    ),
    false,
    'bag_eq(select, select) fail extras & missings',
    '',
    '    Extra records:
        [(](1,Jacob|87,Jackson)[)]
        [(](1,Jacob|87,Jackson)[)]
    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_eq(
        'SELECT 1 AS a, ''foo''::text AS b UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 AS b UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'bag_eq(values, values) fail mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_eq(
        'SELECT 1 AS a UNION ALL SELECT 2 AS b',
        'SELECT ''foo''::text AS a, 1 AS b UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'bag_eq(values, values) fail column count',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

-- Handle failure due to missing dupe.
SELECT * FROM check_test(
    bag_eq(
        'SELECT 1 AS a, ''Anna''::text AS b UNION ALL SELECT 86, ''Angelina'' UNION ALL SELECT 1, ''Anna''',
        'SELECT 1 AS a, ''Anna''::TEXT AS b UNION ALL SELECT 86, ''Angelina'''
    ),
    false,
    'bag_eq(values, values) fail missing dupe',
    '',
    '    Extra records:
        (1,Anna)'
);

/****************************************************************************/
-- Now test set_eq().

SELECT * FROM check_test(
    set_ne(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'whatever'
    ),
    true,
    'set_ne(prepared, select, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_ne(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    true,
    'set_ne(prepared, select)',
    '',
    ''
);

SELECT * FROM check_test(
    set_ne( 'anames', 'expect' ),
    false,
    'set_ne(prepared, prepared) fail',
    '',
    ''
);

-- Handle fail with column mismatch.
SELECT * FROM check_test(
    set_ne(
        'SELECT 1 AS a, ''foo''::text AS b UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 AS b UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'set_ne fail with column mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_ne(
        'SELECT 1 UNION ALL SELECT 2',
        'SELECT ''foo''::text AS a, 1 UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'set_ne fail with different col counts',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

-- Handle fail with a dupe.
SELECT * FROM check_test(
    set_ne(
        'SELECT 1 AS a, ''Anna''::text UNION ALL SELECT 86, ''Angelina'' UNION ALL SELECT 1, ''Anna''',
        'SELECT 1 AS a, ''Anna''::text UNION ALL SELECT 86, ''Angelina'''
    ),
    false,
    'set_ne fail with dupe',
    '',
    ''
);

/****************************************************************************/
-- Now test bag_ne().

SELECT * FROM check_test(
    bag_ne(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'whatever'
    ),
    true,
    'bag_ne(prepared, select, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'anames',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    true,
    'bag_ne(prepared, select)',
    '',
    ''
);

SELECT * FROM check_test(
    bag_ne( 'anames', 'expect' ),
    false,
    'bag_ne(prepared, prepared) fail',
    '',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'SELECT 1 AS a, ''foo''::text UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo'' AS a, 1 UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'bag_ne fail with column mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle pass with a dupe.
SELECT * FROM check_test(
    bag_ne(
        'SELECT 1 AS a, ''Anna''::text UNION ALL SELECT 86, ''Angelina'' UNION ALL SELECT 1, ''Anna''',
        'SELECT 1 AS a, ''Anna''::text UNION ALL SELECT 86, ''Angelina'''
    ),
    true,
    'set_ne pass with dupe',
    '',
    ''
);

-- Handle fail with column mismatch.
SELECT * FROM check_test(
    bag_ne(
        'SELECT 1 AS a, ''foo''::text UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo'' AS a, 1 UNION ALL SELECT ''bar'', 2'
    ),
    false,
    'bag_ne fail with column mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_ne(
        'SELECT 1 UNION SELECT 2',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'bag_ne fail with different col counts',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test results_eq().

PREPARE anames_ord AS SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
PREPARE expect_ord AS SELECT id, name FROM toexpect ORDER BY id;

SELECT * FROM check_test(
    results_eq( 'anames_ord', 'expect_ord', 'whatever' ),
    true,
    'results_eq(prepared, prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    results_eq( 'anames_ord', 'expect_ord' ),
    true,
    'results_eq(prepared, prepared)',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    results_eq( 'EXECUTE anames_ord', 'EXECUTE expect_ord' ),
    true,
    'results_eq(execute, execute)',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM names WHERE name like ''An%'' ORDER BY id',
        'SELECT id, name FROM annames ORDER BY id'
    ),
    true,
    'results_eq(select, select)',
    '',
    ''
);

-- Compare with dupes.
SET client_min_messages = warning;
CREATE table dupes (pk SERIAL PRIMARY KEY, id int, name text);
RESET client_min_messages;
INSERT INTO dupes (id, name) VALUES(1,  'Anna');
INSERT INTO dupes (id, name) VALUES(86, 'Angelina');
INSERT INTO dupes (id, name) VALUES(1,  'Anna');

SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM dupes ORDER BY pk',
        'SELECT id, name FROM dupes ORDER BY pk'
    ),
    true,
    'results_eq(dupe values, dupe values)',
    '',
    ''
);

UPDATE dupes SET name = NULL WHERE pk = 1;
-- Compare with nulls.
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM dupes ORDER BY pk',
        'SELECT id, name FROM dupes ORDER BY pk'
    ),
    true,
    'results_eq(values with null, values with null)',
    '',
    ''
);

UPDATE dupes SET id = NULL, name = NULL;
-- Compare only NULLs
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM dupes LIMIT 2',
        'SELECT id, name FROM dupes LIMIT 2'
    ),
    true,
    'results_eq(nulls, nulls)',
    '',
    ''
);

-- Compare differnt rows of NULLs
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM dupes LIMIT 2',
        'SELECT id, name FROM dupes LIMIT 1'
    ),
    false,
    'results_eq(nulls, nulls) fail',
    '',
    '    Results differ beginning at row 2:
        have: (,)
        want: NULL'
);

-- And now some failures.
SELECT * FROM check_test(
    results_eq(
        'anames_ord',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    false,
    'results_eq(prepared, select) fail',
    '',
    '    Results differ beginning at row 3:
        have: (44,Anna)
        want: (63,Angel)'
);

-- Now when the last row is missing.
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name FROM annames WHERE name <> ''Antonio''',
        'anames_ord'
    ),
    false,
    'results_eq(select, prepared) fail missing last row',
    '',
    '    Results differ beginning at row 7:
        have: NULL
        want: (183,Antonio)'
);

-- Invert that.
SELECT * FROM check_test(
    results_eq(
        'anames_ord',
        'SELECT id, name FROM annames WHERE name <> ''Antonio'''
    ),
    false,
    'results_eq(prepared, select) fail missing first row',
    '',
    '    Results differ beginning at row 7:
        have: (183,Antonio)
        want: NULL'
);

-- Compare with missing dupe.
SET client_min_messages = warning;
CREATE table dubs (pk SERIAL PRIMARY KEY, id int, name text);
RESET client_min_messages;
INSERT INTO dubs (id, name) VALUES(1,  'Anna');
INSERT INTO dubs (id, name) VALUES(86, 'Angelina');
INSERT INTO dubs (id, name) VALUES(1,  'Anna');

SELECT * FROM check_test(
    results_eq(
        'SELECT id, name from dubs ORDER BY pk',
        'SELECT id, name from dubs ORDER BY pk LIMIT 2'
    ),
    false,
    'results_eq(values dupe, values)',
    '',
    '    Results differ beginning at row 3:
        have: (1,Anna)
        want: NULL'
);

UPDATE dubs SET name = NULL WHERE pk = 1;
-- Handle failure with null.
SELECT * FROM check_test(
    results_eq(
        'SELECT id, name from dubs ORDER BY pk LIMIT 2',
        'SELECT id, name from dubs ORDER BY pk DESC LIMIT 2'
    ),
    false,
    'results_eq(values null, values)',
    '',
    '    Results differ beginning at row 1:
        have: (1,)
        want: (1,Anna)'
);

UPDATE dubs SET name = 'foo' WHERE pk = 1;
UPDATE dubs SET name = 'bar' WHERE pk = 2;

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    results_eq(
        'SELECT pk, name from dubs ORDER BY pk LIMIT 2',
        'SELECT name, pk from dubs ORDER BY pk LIMIT 2'
    ),
    false,
    'results_eq(values, values) mismatch',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries:
        have: (1,foo)
        want: (foo,1)
        ERROR: cannot compare dissimilar column types integer and text at record column 1'
    ELSE
      '    Number of columns or their types differ between the queries:
        have: (1,foo)
        want: (foo,1)
        ERROR: details not available in pg <= 9.1'
    END
);

-- Handle failure due to more subtle column mismatch
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, ''foo''::varchar), (2, ''bar''::varchar)',
        'VALUES (1, ''foo''), (2, ''bar'')'
    ),
    false,
    'results_eq(values, values) subtle mismatch',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries
        ERROR: cannot compare dissimilar column types character varying and text at record column 2'
    ELSE
      '    Number of columns or their types differ between the queries
        ERROR: details not available in pg <= 9.1'
    END
);

SELECT * FROM check_test(
    results_eq(
        'VALUES (1::int), (2::int)',
        'VALUES (1::bigint), (2::bigint)'
    ),
    false,
    'results_eq(values, values) integer type mismatch',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries
        ERROR: cannot compare dissimilar column types integer and bigint at record column 1'
    ELSE
      '    Number of columns or their types differ between the queries
        ERROR: details not available in pg <= 9.1'
    END
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    results_eq(
        'SELECT pk from dubs ORDER BY pk LIMIT 2',
        'SELECT pk, name from dubs ORDER BY pk LIMIT 2'
    ),
    false,
    'results_eq(values, values) fail column count',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries:
        have: (1)
        want: (1,foo)
        ERROR: cannot compare record types with different numbers of columns'
    ELSE
      '    Number of columns or their types differ between the queries:
        have: (1)
        want: (1,foo)
        ERROR: details not available in pg <= 9.1'
    END
);

-- Compare with cursors.
DECLARE cwant CURSOR FOR SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
DECLARE chave CURSOR FOR SELECT id, name from annames ORDER BY id;

SELECT * FROM check_test(
    results_eq( 'cwant'::refcursor, 'chave'::refcursor ),
    true,
    'results_eq(cursor, cursor)',
    '',
    ''
);

-- Mix cursors and prepared statements
PREPARE annames_ord AS SELECT id, name FROM annames ORDER BY id;
MOVE BACKWARD ALL IN cwant;

SELECT * FROM check_test(
    results_eq( 'cwant'::refcursor, 'annames_ord' ),
    true,
    'results_eq(cursor, prepared)',
    '',
    ''
);

MOVE BACKWARD ALL IN chave;
SELECT * FROM check_test(
    results_eq( 'annames_ord', 'chave'::refcursor ),
    true,
    'results_eq(prepared, cursor)',
    '',
    ''
);

-- Mix cursor and SQL.
MOVE BACKWARD ALL IN cwant;
SELECT * FROM check_test(
    results_eq( 'cwant'::refcursor, 'SELECT id, name FROM annames ORDER BY id' ),
    true,
    'results_eq(cursor, sql)',
    '',
    ''
);

MOVE BACKWARD ALL IN chave;
SELECT * FROM check_test(
    results_eq( 'SELECT id, name FROM annames ORDER BY id', 'chave'::refcursor ),
    true,
    'results_eq(sql, cursor)',
    '',
    ''
);

/****************************************************************************/
-- Now test set_has().
SELECT * FROM check_test(
    set_has( 'anames', 'expect', 'whatever' ),
    true,
    'set_has( prepared, prepared, description )',
    'whatever',
    ''
);

PREPARE subset AS SELECT id, name FROM toexpect WHERE id IN (11, 44, 63);

SELECT * FROM check_test(
    set_has( 'anames', 'subset' ),
    true,
    'set_has( prepared, subprepared )',
    '',
    ''
);

SELECT * FROM check_test(
    set_has( 'EXECUTE anames', 'EXECUTE subset' ),
    true,
    'set_has( execute, execute )',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    set_has(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM annames'
    ),
    true,
    'set_has( select, select )',
    '',
    ''
);

-- Try an empty set in the second arg.
SELECT * FROM check_test(
    set_has( 'anames', 'SELECT id, name FROM annames WHERE false' ),
    true,
    'set_has( prepared, empty )',
    '',
    ''
);

-- Make sure that dupes are ignored.
SELECT * FROM check_test(
    set_has( 'anames', 'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna''' ),
    true,
    'set_has( prepared, dupes )',
    '',
    ''
);

SELECT * FROM check_test(
    set_has(
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna''',
        'SELECT 44 AS a, ''Anna''::text'
    ),
    true,
    'set_has( dupes, values )',
    '',
    ''
);

-- Check failures.
SELECT * FROM check_test(
    set_has(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'set_has( missing1, expect )',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_has(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'set_has(missing2, expect )',
    '',
    '    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_has(
        'SELECT 1 AS a, ''foo''::text UNION ALL SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 UNION ALL SELECT ''bar'', 2' ),
    false,
    'set_has((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_has(
        'SELECT 1 UNION SELECT 2',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'set_has((int), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test bag_has().
SELECT * FROM check_test(
    bag_has( 'anames', 'expect', 'whatever' ),
    true,
    'bag_has( prepared, prepared, description )',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_has( 'anames', 'subset' ),
    true,
    'bag_has( prepared, subprepared )',
    '',
    ''
);

SELECT * FROM check_test(
    bag_has( 'EXECUTE anames', 'EXECUTE subset' ),
    true,
    'bag_has( execute, execute )',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    bag_has(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM annames'
    ),
    true,
    'bag_has( select, select )',
    '',
    ''
);

-- Try an empty set in the second arg.
SELECT * FROM check_test(
    bag_has( 'anames', 'SELECT id, name FROM annames WHERE false' ),
    true,
    'bag_has( prepared, empty )',
    '',
    ''
);

-- Make sure that dupes are not ignored.
SELECT * FROM check_test(
    bag_has(
        'anames',
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna'''
    ),
    false,
    'bag_has( prepared, dupes )',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_has(
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna''',
        'SELECT 44 AS a, ''Anna''::text'
    ),
    true,
    'bag_has( dupes, values )',
    '',
    ''
);

SELECT * FROM check_test(
    bag_has(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'bag_has( missing1, expect )',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_has(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'bag_has(missing2, expect )',
    '',
    '    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_has(
        'SELECT 1 AS a, ''foo''::text UNION SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'bag_has((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_has(
        'SELECT 1 UNION SELECT 2',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'bag_has((int), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test set_hasnt().

CREATE TABLE folk (id int, name text);
INSERT INTO folk (id, name) VALUES ( 44, 'Larry' );
INSERT INTO folk (id, name) VALUES (52, 'Tom');
INSERT INTO folk (id, name) VALUES (23, 'Damian' );
PREPARE others AS SELECT id, name FROM folk;

SELECT * FROM check_test(
    set_hasnt( 'anames', 'others', 'whatever' ),
    true,
    'set_hasnt( prepared, prepared, description )',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_hasnt( 'anames', 'others' ),
    true,
    'set_hasnt( prepared, prepared, description )',
    '',
    ''
);

SELECT * FROM check_test(
    set_hasnt( 'EXECUTE anames', 'EXECUTE others' ),
    true,
    'set_hasnt( execute, execute )',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    set_hasnt(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM names WHERE name like ''B%'''
    ),
    true,
    'set_hasnt( select, select )',
    '',
    ''
);

-- Try an empty set in the second arg.
SELECT * FROM check_test(
    set_hasnt( 'anames', 'SELECT id, name FROM annames WHERE false' ),
    true,
    'set_hasnt( prepared, empty )',
    '',
    ''
);

-- Make sure that dupes are ignored.
SELECT * FROM check_test(
    set_hasnt( 'anames', 'SELECT 44 AS a, ''Bob''::text UNION ALL SELECT 44, ''Bob''' ),
    true,
    'set_hasnt( prepared, dupes )',
    '',
    ''
);

SELECT * FROM check_test(
    set_hasnt( 'anames', 'SELECT 44 AS a, ''Anna''::text' ),
    false,
    'set_hasnt( prepared, value )',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_hasnt( 'anames', 'SELECT 44 AS a, ''Anna''::text UNION SELECT 86, ''Angelina''' ),
    false,
    'set_hasnt( prepared, values )',
    '',
    '    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_hasnt(
        'SELECT 1 AS a, ''foo''::text UNION SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'set_hasnt((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_hasnt(
        'SELECT 1 UNION SELECT 2',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'set_hasnt((int), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test bag_hasnt().

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'others', 'whatever' ),
    true,
    'bag_hasnt( prepared, prepared, description )',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'others' ),
    true,
    'bag_hasnt( prepared, prepared, description )',
    '',
    ''
);

SELECT * FROM check_test(
    bag_hasnt( 'EXECUTE anames', 'EXECUTE others' ),
    true,
    'bag_hasnt( execute, execute )',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    bag_hasnt(
        'SELECT id, name FROM names WHERE name like ''An%''',
        'SELECT id, name FROM names WHERE name like ''B%'''
    ),
    true,
    'bag_hasnt( select, select )',
    '',
    ''
);

-- Try an empty bag in the second arg.
SELECT * FROM check_test(
    bag_hasnt( 'anames', 'SELECT id, name FROM annames WHERE false' ),
    true,
    'bag_hasnt( prepared, empty )',
    '',
    ''
);

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'SELECT 44 AS a, ''Anna''::text' ),
    false,
    'bag_hasnt( prepared, value )',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'SELECT 44 AS a, ''Anna''::text UNION SELECT 86, ''Angelina''' ),
    false,
    'bag_hasnt( prepared, values )',
    '',
    '    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_hasnt(
        'SELECT 1 AS a, ''foo''::text UNION SELECT 2, ''bar''',
        'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2'
    ),
    false,
    'bag_hasnt((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_hasnt( 'SELECT 1 UNION SELECT 2', 'SELECT ''foo''::text AS a, 1 UNION SELECT ''bar'', 2' ),
    false,
    'bag_hasnt((int), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

-- Make sure that dupes are not ignored.
SELECT * FROM check_test(
    bag_hasnt(
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna''',
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna'''
    ),
    false,
    'bag_hasnt( dupes, dupes )',
    '',
    '    Extra records:
        (44,Anna)
        (44,Anna)'
);

-- But a dupe that appears only once should be in the list only once.
SELECT * FROM check_test(
    bag_hasnt(
        'SELECT 44 AS a, ''Anna''::text',
        'SELECT 44 AS a, ''Anna''::text UNION ALL SELECT 44, ''Anna'''
    ),
    false,
    'bag_hasnt( value, dupes )',
    '',
    '    Extra records:
        (44,Anna)'
);

/****************************************************************************/
-- Test set_eq() with an array argument.
SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ],
        'whatever'
    ),
    true,
    'set_eq(sql, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    true,
    'set_eq(sql, array)',
    '',
    ''
);

SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Andrew', 'Anna' ]
    ),
    true,
    'set_eq(sql, dupe array)',
    '',
    ''
);

-- Fail with an extra record.
SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_eq(sql, array) extra record',
    '',
    '    Extra records:
        (Anthony)'
);

-- Fail with a missing record.
SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Alan', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_eq(sql, array) missing record',
    '',
    '    Missing records:
        (Alan)'
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    set_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY[1, 2, 3]
    ),
    false,
    'set_eq(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

-- Fail with invalid column counts.
SELECT * FROM check_test(
    set_eq(
        'anames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_eq(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text)'
);

/****************************************************************************/
-- Test bag_eq() with an array argument.
SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ],
        'whatever'
    ),
    true,
    'bag_eq(sql, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    true,
    'bag_eq(sql, array)',
    '',
    ''
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    false,
    'bag_eq(sql, dupe array) fail',
    '',
    '    Missing records:
        (Anna)'
);

-- Fail with an extra record.
SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_eq(sql, array) extra record',
    '',
    '    Extra records:
        (Anthony)'
);

-- Fail with a missing record.
SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Alan', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_eq(sql, array) missing record',
    '',
    '    Missing records:
        (Alan)'
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    bag_eq(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY[1, 2, 3]
    ),
    false,
    'bag_eq(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

-- Fail with invalid column counts.
SELECT * FROM check_test(
    bag_eq(
        'anames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_eq(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text)'
);

/****************************************************************************/
-- Test set_ne() with an array argument.
SELECT * FROM check_test(
    set_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna' ],
        'whatever'
    ),
    true,
    'set_ne(sql, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna' ]
    ),
    true,
    'set_ne(sql, array)',
    '',
    ''
);

SELECT * FROM check_test(
    set_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_ne(sql, array) fail',
    '',
    ''
);

-- Fail with dupes.
SELECT * FROM check_test(
    set_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    false,
    'set_ne(sql, dupes array) fail',
    '',
    ''
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    set_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY[1, 2, 3]
    ),
    false,
    'set_ne(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

-- Fail with invalid column counts.
SELECT * FROM check_test(
    set_ne(
        'anames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_ne(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text)'
);

/****************************************************************************/
-- Test bag_ne() with an array argument.
SELECT * FROM check_test(
    bag_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna' ],
        'whatever'
    ),
    true,
    'bag_ne(sql, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna' ]
    ),
    true,
    'bag_ne(sql, array)',
    '',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_ne(sql, array) fail',
    '',
    ''
);

-- Pass with dupes.
SELECT * FROM check_test(
    bag_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    true,
    'bag_ne(sql, dupes array)',
    '',
    ''
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    bag_ne(
        'SELECT name FROM names WHERE name like ''An%''',
        ARRAY[1, 2, 3]
    ),
    false,
    'bag_ne(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

-- Fail with invalid column counts.
SELECT * FROM check_test(
    bag_ne(
        'anames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_ne(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text)'
);

/****************************************************************************/
-- Now test results_eq() with an array argument.

PREPARE anames_only AS SELECT name FROM names WHERE name like 'An%' ORDER BY name;

SELECT * FROM check_test(
    results_eq(
        'anames_only',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ],
        'whatever'
    ),
    true,
    'results_eq(prepared, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    results_eq(
        'anames_only',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ]
    ),
    true,
    'results_eq(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    results_eq(
        'SELECT name FROM names WHERE name like ''An%'' ORDER BY name',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ],
        'whatever'
    ),
    true,
    'results_eq(sql, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    results_eq(
        'SELECT name FROM names WHERE name like ''An%'' ORDER BY name',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ]
    ),
    true,
    'results_eq(sql, array, desc)',
    '',
    ''
);

SELECT * FROM check_test(
    results_eq(
        'anames_only',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony' ]
    ),
    false,
    'results_eq(prepared, array) extra record',
    '',
    '    Results differ beginning at row 7:
        have: (Antonio)
        want: NULL'
);

SELECT * FROM check_test(
    results_eq(
        'SELECT name FROM names WHERE name like ''An%'' AND name <> ''Anna'' ORDER BY name',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ]
    ),
    false,
    'results_eq(select, array) missing record',
    '',
    '    Results differ beginning at row 5:
        have: (Anthony)
        want: (Anna)'
);

/****************************************************************************/
-- Now test results_eq().

PREPARE nenames_ord AS SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
SET client_min_messages = warning;
CREATE TEMPORARY TABLE nord (pk SERIAL PRIMARY KEY, id int, name text);
RESET client_min_messages;
INSERT INTO nord (id, name ) VALUES(15,  'Anthony');
INSERT INTO nord (id, name ) VALUES(44,  'Anna');
INSERT INTO nord (id, name ) VALUES(11,  'Andrew');
INSERT INTO nord (id, name ) VALUES(63,  'Angel');
INSERT INTO nord (id, name ) VALUES(86,  'Angelina');
INSERT INTO nord (id, name ) VALUES(130, 'Andrea');
INSERT INTO nord (id, name ) VALUES(183, 'Antonio');
PREPARE nexpect_ord AS SELECT id, name FROM nord ORDER BY pk;

SELECT * FROM check_test(
    results_ne( 'nenames_ord', 'nexpect_ord', 'whatever' ),
    true,
    'results_ne(prepared, prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    results_ne( 'nenames_ord', 'nexpect_ord' ),
    true,
    'results_ne(prepared, prepared)',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    results_ne( 'EXECUTE nenames_ord', 'EXECUTE nexpect_ord' ),
    true,
    'results_ne(execute, execute)',
    '',
    ''
);

-- Compare actual SELECT statements.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM names WHERE name like ''An%'' ORDER BY id',
        'SELECT id, name FROM annames WHERE name <> ''Anna'' ORDER BY id'
    ),
    true,
    'results_ne(select, select)',
    '',
    ''
);

UPDATE dubs SET name = 'Anna' WHERE pk = 1;
UPDATE dubs SET name = 'Angelina', id = 86 WHERE pk = 2;

SET client_min_messages = warning;
CREATE table buds (pk SERIAL PRIMARY KEY, id int, name text);
RESET client_min_messages;
INSERT INTO buds (id, name) VALUES(2,  'Anna');
INSERT INTO buds (id, name) VALUES(86, 'Angelina');
INSERT INTO buds (id, name) VALUES(2,  'Anna');

-- Compare with dupes.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM dubs ORDER BY pk',
        'SELECT id, name FROM buds ORDER BY pk'
    ),
    true,
    'results_ne(dupe values, dupe values)',
    '',
    ''
);

UPDATE dubs SET id = 4, name = NULL WHERE pk = 1;
UPDATE dubs SET name = NULL WHERE pk = 3;
UPDATE buds SET id = 4, name = NULL WHERE pk = 1;

-- Compare with nulls.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM dubs ORDER BY pk',
        'SELECT id, name FROM buds ORDER BY pk'
    ),
    true,
    'results_ne(values with null, values with null)',
    '',
    ''
);

-- Compare only NULLs
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM dupes LIMIT 3',
        'SELECT id, name FROM dupes LIMIT 2'
    ),
    true,
    'results_ne(nulls, nulls)',
    '',
    ''
);

-- And now a failure.
SELECT * FROM check_test(
    results_ne(
        'nenames_ord',
        'SELECT id, name FROM annames'
    ),
    false,
    'results_ne(prepared, select) fail',
    '',
    ''
);

-- Now when the last row is missing.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM annames WHERE name <> ''Antonio''',
        'nenames_ord'
    ),
    true,
    'results_ne(select, prepared) missing last row',
    '',
    ''
);

-- Invert that.
SELECT * FROM check_test(
    results_ne(
        'nenames_ord',
        'SELECT id, name FROM annames WHERE name <> ''Antonio'''
    ),
    true,
    'results_ne(prepared, select) missing first row',
    '',
    ''
);

UPDATE dubs SET id = 1, name = 'Anna' WHERE pk IN (1, 3);

-- Compare with missing dupe.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM dubs ORDER BY pk',
        'SELECT id, name FROM dubs ORDER BY pk LIMIT 2'
    ),
    true,
    'results_ne(values dupe, values)',
    '',
    ''
);

UPDATE dubs SET name = NULL where PK = 2;
-- Handle pass with null.
SELECT * FROM check_test(
    results_ne(
        'SELECT id, name FROM dubs ORDER BY pk LIMIT 2',
        'SELECT id, name FROM buds ORDER BY pk LIMIT 2'
    ),
    true,
    'results_ne(values null, values)',
    '',
    ''
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    results_ne( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'results_ne(values, values) mismatch',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries:
        have: (1,foo)
        want: (foo,1)
        ERROR: cannot compare dissimilar column types integer and text at record column 1'
    ELSE
      '    Number of columns or their types differ between the queries:
        have: (1,foo)
        want: (foo,1)
        ERROR: details not available in pg <= 9.1'
    END
);

-- Handle failure due to subtle column mismatch.
SELECT * FROM check_test(
    results_ne(
        'VALUES (1, ''foo''::varchar), (2, ''bar''::varchar)',
        'VALUES (1, ''foo''), (2, ''bar'')'
    ),
    false,
    'results_ne(values, values) subtle mismatch',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries
        ERROR: cannot compare dissimilar column types character varying and text at record column 2'
    ELSE
      '    Number of columns or their types differ between the queries
        ERROR: details not available in pg <= 9.1'
    END
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    results_ne( 'VALUES (1), (2)', 'VALUES (1, ''foo''), (2, ''bar'')' ),
    false,
    'results_ne(values, values) fail column count',
    '',
    CASE WHEN pg_version_num() >= 90200 THEN
      '    Number of columns or their types differ between the queries:
        have: (1)
        want: (1,foo)
        ERROR: cannot compare record types with different numbers of columns'
    ELSE
      '    Number of columns or their types differ between the queries:
        have: (1)
        want: (1,foo)
        ERROR: details not available in pg <= 9.1'
    END
);

-- Compare with cursors.
CLOSE cwant;
CLOSE chave;
DECLARE cwant CURSOR FOR SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
DECLARE chave CURSOR FOR SELECT id, name from annames ORDER BY id;

SELECT * FROM check_test(
    results_ne( 'cwant'::refcursor, 'chave'::refcursor ),
    false,
    'results_ne(cursor, cursor)',
    '',
    ''
);

-- Mix cursors and prepared statements
DEALLOCATE annames_ord;
PREPARE annames_ord AS SELECT id, name FROM annames ORDER BY id;
MOVE BACKWARD ALL IN cwant;

SELECT * FROM check_test(
    results_ne( 'cwant'::refcursor, 'annames_ord' ),
    false,
    'results_ne(cursor, prepared)',
    '',
    ''
);

MOVE BACKWARD ALL IN chave;
SELECT * FROM check_test(
    results_ne( 'annames_ord', 'chave'::refcursor ),
    false,
    'results_ne(prepared, cursor)',
    '',
    ''
);

-- Mix cursor and SQL.
SELECT * FROM check_test(
    results_ne( 'cwant'::refcursor, 'SELECT id, name FROM annames ORDER BY id' ),
    true,
    'results_ne(cursor, sql)',
    '',
    ''
);

MOVE BACKWARD ALL IN chave;
SELECT * FROM check_test(
    results_ne( 'SELECT id, name FROM annames ORDER BY id', 'chave'::refcursor ),
    false,
    'results_ne(sql, cursor)',
    '',
    ''
);

/****************************************************************************/
-- Now test is_empty().
SELECT * FROM check_test(
    is_empty( 'SELECT 1 WHERE FALSE', 'whatever' ),
    true,
    'is_empty(sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_empty( 'SELECT 1 WHERE FALSE' ),
    true,
    'is_empty(sql)',
    '',
    ''
);

PREPARE emptyset AS SELECT * FROM names WHERE FALSE;
SELECT * FROM check_test(
    is_empty( 'emptyset', 'whatever' ),
    true,
    'is_empty(prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_empty( 'emptyset' ),
    true,
    'is_empty(prepared)',
    '',
    ''
);

CREATE FUNCTION test_empty_fail() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    PREPARE notempty AS SELECT id, name FROM names WHERE name IN ('Jacob', 'Emily')
        ORDER BY ID;
    FOR tap IN SELECT * FROM check_test(
        is_empty( 'notempty', 'whatever' ),
        false,
        'is_empty(prepared, desc) fail',
        'whatever',
        '    Unexpected records:
        (1,Jacob)
        (2,Emily)'
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        is_empty( 'notempty' ),
        false,
        'is_empty(prepared) fail',
        '',
        '    Unexpected records:
        (1,Jacob)
        (2,Emily)'
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;
SELECT * FROM test_empty_fail();

/****************************************************************************/
-- Now test isnt_empty().
SELECT * FROM check_test(
    isnt_empty( 'SELECT 1', 'whatever' ),
    true,
    'isnt_empty(sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'SELECT 1 WHERE FALSE', 'whatever' ),
    false,
    'isnt_empty(sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'SELECT 1 WHERE FALSE' ),
    false,
    'isnt_empty(sql)',
    '',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'SELECT 1' ),
    true,
    'isnt_empty(sql)',
    '',
    ''
);

PREPARE someset(boolean) AS SELECT * FROM names WHERE $1;
SELECT * FROM check_test(
    isnt_empty( 'EXECUTE someset(true)', 'whatever' ),
    true,
    'isnt_empty(prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'EXECUTE someset(false)', 'whatever' ),
    false,
    'isnt_empty(prepared, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'EXECUTE someset(true)' ),
    true,
    'isnt_empty(prepared)',
    '',
    ''
);

SELECT * FROM check_test(
    isnt_empty( 'EXECUTE someset(false)' ),
    false,
    'isnt_empty(prepared)',
    '',
    ''
);

/****************************************************************************/
-- Test row_eq().
PREPARE arow AS SELECT id, name FROM names WHERE name = 'Jacob';
CREATE TYPE sometype AS (
    id    INT,
    name  TEXT
);

CREATE FUNCTION test_row_eq() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    FOR tap IN SELECT * FROM check_test(
        row_eq('arow', ROW(1, 'Jacob')::names, 'whatever'),
        true,
        'row_eq(prepared, record, desc)',
        'whatever',
        ''
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        row_eq('SELECT id, name FROM names WHERE id = 1', ROW(1, 'Jacob')::names, 'whatever'),
        true,
        'row_eq(sql, record, desc)',
        'whatever',
        ''
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        row_eq('arow', ROW(1, 'Jacob')::names),
        true,
        'row_eq(prepared, record, desc)',
        '',
        ''
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        row_eq('arow', ROW(1, 'Larry')::names),
        false,
        'row_eq(prepared, record, desc)',
        '',
        '        have: (1,Jacob)
        want: (1,Larry)'
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        row_eq('arow', ROW(1, 'Jacob')::sometype),
        true,
        'row_eq(prepared, sometype, desc)',
        '',
        ''
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        row_eq('SELECT 1, ''Jacob''::text', ROW(1, 'Jacob')::sometype),
        true,
        'row_eq(sqlrow, sometype, desc)',
        '',
        ''
    ) AS a(b) LOOP
        RETURN NEXT tap.b;
    END LOOP;

    INSERT INTO someat (ts) values ('2009-12-04T07:22:52');
    IF pg_version_num() < 110000 THEN
        -- Prior to 11, one cannot pass a bare RECORD.
        RETURN NEXT throws_ok(
            'SELECT row_eq( ''SELECT id, ts FROM someat'', ROW(1, ''2009-12-04T07:22:52''::timestamp) )',
            '0A000'
            --  'PL/pgSQL functions cannot accept type record'
        );
        RETURN NEXT pass('row_eq(qry, record) should pass');
        RETURN NEXT pass('row_eq(qry, record) should have the proper description');
        RETURN NEXT pass('row_eq(qry, record) should have the proper diagnostics');
    ELSE
        -- Postgres 11 supports record arguments!
        RETURN NEXT pass('threw 0A000');
        FOR tap IN SELECT * FROM check_test(
            row_eq('SELECT id, ts FROM someat', ROW(1, '2009-12-04T07:22:52'::timestamp)),
            true,
            'row_eq(qry, record)',
            '',
            ''
        ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;

SELECT * FROM test_row_eq();

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
