\unset ECHO
\i test_setup.sql

SELECT plan(274);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

CREATE TABLE names (
    id    SERIAL NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
);

RESET client_min_messages;

-- Top 100 boy an 100 girl names in 2005. http://www.ssa.gov/OACT/babynames/
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

PREPARE "something cool" AS VALUES (1, 2), (3, 4);
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
    'simple set test',
    'whatever',
    ''
);

SELECT * FROM check_test(
    set_eq( 'anames', 'expect' ),
    true,
    'simple set test without descr',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    set_eq( 'EXECUTE anames', 'EXECUTE expect' ),
    true,
    'execute set test',
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
    'select set test',
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
    'dupe rows ignored in set test',
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
    'fail with extra record',
    '',
    '   Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')'
    ),
    false,
    'fail with 2 extra records',
    '',
    E'   Extra records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'fail with missing record',
    '',
    '   Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'fail with 2 missing records',
    '',
    E'   Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);


SELECT * FROM check_test(
    set_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob)'' AND name <> ''Anna''',
        'SELECT id, name FROM annames'
    ),
    false,
    'fail with extra and missing',
    '',
    '   Extra records:
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
    'fail with 2 extras and 2 missings',
    '',
    E'   Extra records:
        \\((1,Jacob|87,Jackson)\\)
        \\((1,Jacob|87,Jackson)\\)
    Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    set_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with column mismatch',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    set_eq( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with different col counts',
    '',
    '   Columns differ between queries:
        have: (integer)
        want: (text,integer)'
);

/****************************************************************************/
-- Now test bag_eq().

SELECT * FROM check_test(
    bag_eq( 'anames', 'expect', 'whatever' ),
    true,
    'simple bag test',
    'whatever',
    ''
);

SELECT * FROM check_test(
    bag_eq( 'anames', 'expect' ),
    true,
    'simple bag test without descr',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    bag_eq( 'EXECUTE anames', 'EXECUTE expect' ),
    true,
    'execute bag test',
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
    'select bag test',
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
    'bag test with dupes',
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
    'fail with extra record',
    '',
    '   Extra records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_eq(
        'anames',
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')'
    ),
    false,
    'fail with 2 extra records',
    '',
    E'   Extra records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM annames WHERE name <> ''Anna''',
        'expect'
    ),
    false,
    'fail with missing record',
    '',
    '   Missing records:
        (44,Anna)'
);

SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM annames WHERE name NOT IN (''Anna'', ''Angelina'')',
        'expect'
    ),
    false,
    'fail with 2 missing records',
    '',
    E'   Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);


SELECT * FROM check_test(
    bag_eq(
        'SELECT id, name FROM names WHERE name ~ ''^(An|Jacob)'' AND name <> ''Anna''',
        'SELECT id, name FROM annames'
    ),
    false,
    'fail with extra and missing',
    '',
    '   Extra records:
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
    'fail with 2 extras and 2 missings',
    '',
    E'   Extra records:
        \\((1,Jacob|87,Jackson)\\)
        \\((1,Jacob|87,Jackson)\\)
    Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    bag_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with column mismatch',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    bag_eq( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with different col counts',
    '',
    '   Columns differ between queries:
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
    'bag fail with missing dupe',
    '',
    '   Extra records:
        (1,Anna)'
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
    'simple results test',
    'whatever',
    ''
);

SELECT * FROM check_test(
    results_eq( 'anames_ord', 'expect_ord' ),
    true,
    'simple results test without desc',
    '',
    ''
);

-- Pass a full SQL statement for the prepared statements.
SELECT * FROM check_test(
    results_eq( 'EXECUTE anames_ord', 'EXECUTE expect_ord' ),
    true,
    'execute results test',
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
    'select results test',
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
    'results test with dupes',
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
    'results test with nulls',
    '',
    ''
);

-- And now some failures.
SELECT * FROM check_test(
    results_eq(
        'anames_ord',
        'SELECT id, name FROM annames WHERE name <> ''Anna'''
    ),
    false,
    'fail with extra record',
    '',
    '   Results differ beginning at row 3:
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
    'fail with missing record',
    '',
    '   Results differ beginning at row 7:
        have: ()
        want: (183,Antonio)'
);

-- Invert that.
SELECT * FROM check_test(
    results_eq(
        'anames_ord',
        'SELECT id, name FROM annames WHERE name <> ''Antonio'''
    ),
    false,
    'fail with missing record',
    '',
    '   Results differ beginning at row 7:
        have: (183,Antonio)
        want: ()'
);


-- Compare with missing dupe.
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, ''Anna''), (86, ''Angelina''), (1, ''Anna'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    false,
    'results fail with missing dupe',
    '',
    '   Results differ beginning at row 3:
        have: (1,Anna)
        want: ()'
);

-- Handle failure with null.
SELECT * FROM check_test(
    results_eq(
        'VALUES (1, NULL), (86, ''Angelina'')',
        'VALUES (1, ''Anna''), (86, ''Angelina'')'
    ),
    false,
    'fail with NULL',
    '',
    '   Results differ beginning at row 1:
        have: (1,)
        want: (1,Anna)'
);


-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    results_eq( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with column mismatch',
    '',
    CASE WHEN pg_version_num() < 80400 THEN '   Results differ beginning at row 1:' ELSE '   Columns differ between queries:' END || '
        have: (1,foo)
        want: (foo,1)'
);

-- Handle falure due to more subtle column mismatch, valid only on 8.4.
CREATE OR REPLACE FUNCTION subtlefail() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() < 80400 THEN
        -- 8.3 and earlier cast records to text, so subtlety is out.
        RETURN NEXT pass('fail with subtle column mismatch should fail');
        RETURN NEXT pass('fail with subtle column mismatch should have the proper description');
        RETURN NEXT pass('fail with subtle column mismatch should have the proper diagnostics');
    ELSE
        -- 8.4 does true record comparisions, yay!
        FOR tap IN SELECT * FROM check_test(
            results_eq(
                'VALUES (1, ''foo''::varchar), (2, ''bar''::varchar)',
                'VALUES (1, ''foo''), (2, ''bar'')'
            ),
            false,
            'fail with subtle column mismatch',
            '',
            '   Columns differ between queries:
        have: (1,foo)
        want: (1,foo)' ) AS a(b) LOOP
            RETURN NEXT tap.b;
        END LOOP;
    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;
SELECT * FROM subtlefail();

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    results_eq( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'fail with different col counts',
    '',
    CASE WHEN pg_version_num() < 80400 THEN '   Results differ beginning at row 1:' ELSE '   Columns differ between queries:' END || '
        have: (1)
        want: (foo,1)'
);

-- Compare with cursors.
DECLARE cwant CURSOR FOR SELECT id, name FROM names WHERE name like 'An%' ORDER BY id;
DECLARE chave CURSOR FOR SELECT id, name from annames ORDER BY id;

SELECT * FROM check_test(
    results_eq( 'cwant'::refcursor, 'chave'::refcursor ),
    true,
    'simple results with cursors',
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
    '   Missing records:
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
    E'   Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    set_has( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_has((int,text), (text,int))',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    set_has( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_has((int), (text,int))',
    '',
    '   Columns differ between queries:
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
    bag_has( 'anames', 'VALUES (44, ''Anna''), (44, ''Anna'')' ),
    false,
    'bag_has( prepared, dupes )',
    '',
    '   Missing records:
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
    '   Missing records:
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
    E'   Missing records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    bag_has( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_has((int,text), (text,int))',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    bag_has( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_has((int), (text,int))',
    '',
    '   Columns differ between queries:
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
    set_hasnt( 'anames', 'VALUES (44, ''Bob''), (44, ''Bob'')' ),
    true,
    'set_hasnt( prepared, dupes )',
    '',
    ''
);

PREPARE overlap AS VALUES ( 44, 'Larry' ), (52, 'Tom'), (23, 'Damian' );

SELECT * FROM check_test(
    set_hasnt( 'anames', 'VALUES (44,''Anna'')' ),
    false,
    'set_hasnt( prepared, value )',
    '',
    '   Extra records:
        (44,Anna)'
);


SELECT * FROM check_test(
    set_hasnt( 'anames', 'VALUES (44, ''Anna''), (86, ''Angelina'')' ),
    false,
    'set_hasnt( prepared, values )',
    '',
    E'   Extra records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    set_hasnt( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_hasnt((int,text), (text,int))',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    set_hasnt( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'set_hasnt((int), (text,int))',
    '',
    '   Columns differ between queries:
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
    bag_hasnt( 'anames', 'VALUES (44,''Anna'')' ),
    false,
    'bag_hasnt( prepared, value )',
    '',
    '   Extra records:
        (44,Anna)'
);


SELECT * FROM check_test(
    bag_hasnt( 'anames', 'VALUES (44, ''Anna''), (86, ''Angelina'')' ),
    false,
    'bag_hasnt( prepared, values )',
    '',
    E'   Extra records:
        \\((44,Anna|86,Angelina)\\)
        \\((44,Anna|86,Angelina)\\)',
    true
);

-- Handle falure due to column mismatch.
SELECT * FROM check_test(
    bag_hasnt( 'VALUES (1, ''foo''), (2, ''bar'')', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_hasnt((int,text), (text,int))',
    '',
    '   Columns differ between queries:
        have: (integer,text)
        want: (text,integer)'
);

-- Handle falure due to column count mismatch.
SELECT * FROM check_test(
    bag_hasnt( 'VALUES (1), (2)', 'VALUES (''foo'', 1), (''bar'', 2)' ),
    false,
    'bag_hasnt((int), (text,int))',
    '',
    '   Columns differ between queries:
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
    '   Extra records:
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
    '   Extra records:
        (44,Anna)'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
