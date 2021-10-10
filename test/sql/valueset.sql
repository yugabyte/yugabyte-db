\unset ECHO
\i test/setup.sql

SELECT plan(349);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

CREATE TABLE names (
    id    SERIAL NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
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
PREPARE expect AS VALUES (11, 'Andrew'), ( 44, 'Anna'), (15, 'Anthony'),
                         (183, 'Antonio'), (86, 'Angelina'), (130, 'Andrea'),
                         (63, 'Angel');

/****************************************************************************/
-- First, test _temptable.

PREPARE "something cool" AS VALUES (1, 2), (3, 4);
SELECT is(
    _temptable( '"something cool"', '__spacenames__' ),
    '__spacenames__',
    'Should create a temp table for a prepared statement with space and values'
);
SELECT has_table('__spacenames__' );

SELECT is(
    _temptable('VALUES (1, 2), (3, 5)', '__somevals__'),
    '__somevals__',
    'Should create a temp table for a values statement'
);
SELECT has_table('__somevals__');

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

-- Make sure that dupes are disregarded.
SELECT * FROM check_test(
    set_eq(
        'VALUES (1, ''Anna'')',
        'VALUES (1, ''Anna''), (1, ''Anna'')'
    ),
    true,
    'set_eq(values, dupe values)',
    '',
    ''
);

-- Try some failures.
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
    E'    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_eq(values, values) fail mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_eq( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_eq(values, values) fail column count',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
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

-- Compare with dupes.
SELECT * FROM check_test(
    bag_eq(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (1, ''Anna''), (86, ''Angelina'')'
    ),
    true,
    'bag_eq(dupe values, dupe values)',
    '',
    ''
);

-- And now some failures.
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
    E'    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_eq(values, values) fail mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_eq( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
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
    set_ne( 'anames', 'expect' ),
    false,
    'set_ne(prepared, prepared) fail',
    '',
    ''
);

-- Handle fail with column mismatch.
SELECT * FROM check_test(
    set_ne( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_ne fail with column mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_ne( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    false,
    'set_ne fail with dupe',
    '',
    ''
);

/****************************************************************************/
-- Now test bag_ne().

SELECT * FROM check_test(
    bag_ne( 'anames', 'expect' ),
    false,
    'bag_ne(prepared, prepared) fail',
    '',
    ''
);

SELECT * FROM check_test(
    bag_ne( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    true,
    'set_ne pass with dupe',
    '',
    ''
);

-- Handle fail with column mismatch.
SELECT * FROM check_test(
    bag_ne( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_ne fail with column mismatch',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_ne( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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
PREPARE expect_ord AS VALUES (11, 'Andrew'),  (15, 'Anthony'), ( 44, 'Anna'),
                         (63, 'Angel'), (86, 'Angelina'), (130, 'Andrea'),
                         (183, 'Antonio');

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

-- Compare with dupes.
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')'
    ),
    true,
    'results_eq(dupe values, dupe values)',
    '',
    ''
);

-- Compare with nulls.
SELECT * FROM check_test(
    results_eq(
        'VALUES (4, NULL), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (4, NULL), (86, ''Angelina''), (1, ''Anna'')'
    ),
    true,
    'results_eq(values with null, values with null)',
    '',
    ''
);

-- Compare only NULLs
SELECT * FROM check_test(
    results_eq(
        'VALUES (NULL, NULL), (NULL, NULL)',
        'VALUES (NULL, NULL), (NULL, NULL)'
    ),
    true,
    'results_eq(nulls, nulls)',
    '',
    ''
);

-- Compare only NULLs
SELECT * FROM check_test(
    results_eq(
        'VALUES (NULL, NULL), (NULL, NULL)',
        'VALUES (NULL, NULL)'
    ),
    false,
    'results_eq(nulls, nulls) fail',
    '',
    '    Results differ beginning at row 2:
        have: (,)
        want: NULL'
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
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    false,
    'results_eq(values dupe, values)',
    '',
    '    Results differ beginning at row 3:
        have: (1,Anna)
        want: NULL'
);

-- Handle failure with null.
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, NULL), (86, ''Angelina'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    false,
    'results_eq(values null, values)',
    '',
    '    Results differ beginning at row 1:
        have: (1,)
        want: (1,Anna)'
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    results_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    results_eq( 'VALUES (1), (2)', 'VALUES (1, ''foo''), (2, ''bar'')' ),
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

-- Mix cursors and prepared statements
PREPARE annames_ord AS VALUES (11, 'Andrew'), (15, 'Anthony'), ( 44, 'Anna'),
                              (63, 'Angel'), (86, 'Angelina'), (130, 'Andrea'),
                              (183, 'Antonio');
MOVE BACKWARD ALL IN cwant;

SELECT * FROM check_test(
    results_eq( 'cwant'::refcursor, 'annames_ord' ),
    true,
    'results_eq(cursor, prepared)',
    '',
    ''
);

SELECT * FROM check_test(
    results_eq( 'annames_ord', 'chave'::refcursor ),
    true,
    'results_eq(prepared, cursor)',
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

PREPARE subset AS VALUES (11, 'Andrew'), ( 44, 'Anna'), (63, 'Angel');

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

-- Make sure that dupes are ignored.
SELECT * FROM check_test(
    set_has( 'anames', 'VALUES (44, ''Anna''), (44, ''Anna'')' ),
    true,
    'set_has( prepared, dupes )',
    '',
    ''
);

SELECT * FROM check_test(
    set_has( 'VALUES (44, ''Anna''), (44, ''Anna'')', 'VALUES(44, ''Anna'')' ),
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
    E'    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_has( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_has((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_has( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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

-- Make sure that dupes are not ignored.
SELECT * FROM check_test(
    bag_has( 'anames', 'VALUES (44, ''Anna''), (44, ''Anna'')' ),
    false,
    'bag_has( prepared, dupes )',
    '',
    '    Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_has( 'VALUES (44, ''Anna''), (44, ''Anna'')', 'VALUES(44, ''Anna'')' ),
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
    E'    Missing records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_has( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_has((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_has( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_has((int), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test set_hasnt().

PREPARE others AS VALUES ( 44, 'Larry' ), (52, 'Tom'), (23, 'Damian' );

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

-- Make sure that dupes are ignored.
SELECT * FROM check_test(
    set_hasnt( 'anames', 'VALUES (44, ''Bob''), (44, ''Bob'')' ),
    true,
    'set_hasnt( prepared, dupes )',
    '',
    ''
);

SELECT * FROM check_test(
    set_hasnt( 'anames', 'VALUES (44,''Anna'')' ),
    false,
    'set_hasnt( prepared, value )',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_hasnt( 'anames', 'VALUES (44, ''Anna''), (86, ''Angelina'')' ),
    false,
    'set_hasnt( prepared, values )',
    '',
    E'    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    set_hasnt( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_hasnt((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    set_hasnt( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'VALUES (44,''Anna'')' ),
    false,
    'bag_hasnt( prepared, value )',
    '',
    '    Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_hasnt( 'anames', 'VALUES (44, ''Anna''), (86, ''Angelina'')' ),
    false,
    'bag_hasnt( prepared, values )',
    '',
    E'    Extra records:
        [(](44,Anna|86,Angelina)[)]
        [(](44,Anna|86,Angelina)[)]',
    true
);

-- Handle failure due to column mismatch.
SELECT * FROM check_test(
    bag_hasnt( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_hasnt((int,text), (text,int))',
    '',
    '    Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle failure due to column count mismatch.
SELECT * FROM check_test(
    bag_hasnt( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
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
        'VALUES (44, ''Anna''), (44, ''Anna'')',
        'VALUES (44, ''Anna''), (44, ''Anna'')'
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
        'VALUES (44, ''Anna'')',
        'VALUES (44, ''Anna''), (44, ''Anna'')'
    ),
    false,
    'bag_hasnt( value, dupes )',
    '',
    '    Extra records:
        (44,Anna)'
);

/****************************************************************************/
-- Test set_eq() with an array argument.
PREPARE justnames AS
 VALUES ('Andrew'), ('Antonio'), ('Angelina'), ('Anna'), ('Anthony'), ('Andrea'), ('Angel');
SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ],
        'whatever'
    ),
    true,
    'set_eq(prepared, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    true,
    'set_eq(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Andrew', 'Anna' ]
    ),
    true,
    'set_eq(prepared, dupe array)',
    '',
    ''
);

-- Fail with an extra record.
SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_eq(prepared, array) extra record',
    '',
    '    Extra records:
        (Anthony)'
);

-- Fail with a missing record.
SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Alan', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_eq(prepared, array) missing record',
    '',
    '    Missing records:
        (Alan)'
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    set_eq(
        'justnames',
        ARRAY[1, 2, 3]
    ),
    false,
    'set_eq(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

/****************************************************************************/
-- Test bag_eq() with an array argument.
SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ],
        'whatever'
    ),
    true,
    'bag_eq(prepared, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    true,
    'bag_eq(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    false,
    'bag_eq(prepared, dupe array) fail',
    '',
    '    Missing records:
        (Anna)'
);

-- Fail with an extra record.
SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_eq(prepared, array) extra record',
    '',
    '    Extra records:
        (Anthony)'
);

-- Fail with a missing record.
SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Alan', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_eq(prepared, array) missing record',
    '',
    '    Missing records:
        (Alan)'
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    bag_eq(
        'justnames',
        ARRAY[1, 2, 3]
    ),
    false,
    'bag_eq(prepared, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

/****************************************************************************/
-- Test set_ne() with an array argument.
SELECT * FROM check_test(
    set_ne(
        'justnames',
        ARRAY['Andrew', 'Anna' ],
        'whatever'
    ),
    true,
    'set_ne(prepared, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_ne(
        'justnames',
        ARRAY['Andrew', 'Anna' ]
    ),
    true,
    'set_ne(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    set_ne(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'set_ne(prepared, array) fail',
    '',
    ''
);

-- Fail with dupes.
SELECT * FROM check_test(
    set_ne(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    false,
    'set_ne(prepared, dupes array) fail',
    '',
    ''
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    set_ne(
        'justnames',
        ARRAY[1, 2, 3]
    ),
    false,
    'set_ne(sql, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

/****************************************************************************/
-- Test bag_ne() with an array argument.
SELECT * FROM check_test(
    bag_ne(
        'justnames',
        ARRAY['Andrew', 'Anna' ],
        'whatever'
    ),
    true,
    'bag_ne(prepared, array, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'justnames',
        ARRAY['Andrew', 'Anna' ]
    ),
    true,
    'bag_ne(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    bag_ne(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel' ]
    ),
    false,
    'bag_ne(prepared, array) fail',
    '',
    ''
);

-- Pass with dupes.
SELECT * FROM check_test(
    bag_ne(
        'justnames',
        ARRAY['Andrew', 'Anna', 'Anthony', 'Antonio', 'Angelina', 'Andrea', 'Angel', 'Anna' ]
    ),
    true,
    'bag_ne(prepared, dupes array)',
    '',
    ''
);

-- Fail with incompatible columns.
SELECT * FROM check_test(
    bag_ne(
        'justnames',
        ARRAY[1, 2, 3]
    ),
    false,
    'bag_ne(prepared, array) incompatible types',
    '',
    '    Columns differ between queries:
        have: (text)
        want: (integer)'
);

/****************************************************************************/
-- Now test results_eq() with an array argument.

PREPARE ordnames AS
 VALUES ('Andrea'), ('Andrew'), ('Angel'), ('Angelina'), ('Anna'), ('Anthony'), ('Antonio');

SELECT * FROM check_test(
    results_eq(
        'ordnames',
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
        'ordnames',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony', 'Antonio' ]
    ),
    true,
    'results_eq(prepared, array)',
    '',
    ''
);

SELECT * FROM check_test(
    results_eq(
        'ordnames',
        ARRAY['Andrea', 'Andrew', 'Angel', 'Angelina', 'Anna', 'Anthony' ]
    ),
    false,
    'results_eq(prepared, array) extra record',
    '',
    '    Results differ beginning at row 7:
        have: (Antonio)
        want: NULL'
);

/****************************************************************************/
-- Now test results_eq().

PREPARE nenames_ord AS SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
PREPARE nexpect_ord AS VALUES (15, 'Anthony'), ( 44, 'Anna'), (11, 'Andrew'),
                         (63, 'Angel'), (86, 'Angelina'), (130, 'Andrea'),
                         (183, 'Antonio');

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

-- Compare with dupes.
SELECT * FROM check_test(
    results_ne(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (2, ''Anna''), (86, ''Angelina''), (2, ''Anna'')'
    ),
    true,
    'results_ne(dupe values, dupe values)',
    '',
    ''
);

-- Compare with nulls.
SELECT * FROM check_test(
    results_ne(
        'VALUES (4, NULL), (86, ''Angelina''), (1, NULL)',
        'VALUES (4, NULL), (86, ''Angelina''), (1, ''Anna'')'
    ),
    true,
    'results_ne(values with null, values with null)',
    '',
    ''
);

-- Compare only NULLs
SELECT * FROM check_test(
    results_ne(
        'VALUES (NULL, NULL), (NULL, NULL), (NULL, NULL)',
        'VALUES (NULL, NULL), (NULL, NULL)'
    ),
    true,
    'results_ne(nulls, nulls)',
    '',
    ''
);

-- Compare with missing dupe.
SELECT * FROM check_test(
    results_ne(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    true,
    'results_ne(values dupe, values)',
    '',
    ''
);

-- Handle pass with null.
SELECT * FROM check_test(
    results_ne(
        'VALUES (1, NULL), (86, ''Angelina'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
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

-- Mix cursors and prepared statements
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

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
