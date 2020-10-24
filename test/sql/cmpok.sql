\unset ECHO
\i test/setup.sql

SELECT plan(38);

/****************************************************************************/
-- Test cmp_ok().
SELECT * FROM check_test(
    cmp_ok( 1, '=', 1, '1 should = 1' ),
    true,
    'cmp_ok( int, =, int )',
    '1 should = 1',
    ''
);

SELECT * FROM check_test(
    cmp_ok( 1, '<>', 2, '1 should <> 2' ),
    true,
    'cmp_ok( int, <>, int )',
    '1 should <> 2',
    ''
);

SELECT * FROM check_test(
    cmp_ok( '((0,0),(1,1))'::polygon, '~=', '((1,1),(0,0))'::polygon ),
    true,
    'cmp_ok( polygon, ~=, polygon )'
    '',
    ''
);

SELECT * FROM check_test(
    cmp_ok( ARRAY[1, 2], '=', ARRAY[1, 2]),
    true,
    'cmp_ok( int[], =, int[] )',
    '',
    ''
);

SELECT * FROM check_test(
    cmp_ok( ARRAY['192.168.1.2'::inet], '=', ARRAY['192.168.1.2'::inet] ),
    true,
    'cmp_ok( inet[], =, inet[] )',
    '',
    ''
);

SELECT * FROM check_test(
    cmp_ok( 1, '=', 2, '1 should = 2' ),
    false,
    'cmp_ok() fail',
    '1 should = 2',
    '    ''1''
        =
    ''2'''
);

SELECT * FROM check_test(
    cmp_ok( 1, '=', NULL, '1 should = NULL' ),
    false,
    'cmp_ok() NULL fail',
    '1 should = NULL',
    '    ''1''
        =
    NULL'
);


/****************************************************************************/
-- Test isa_ok().
SELECT * FROM check_test(
    isa_ok( ''::text, 'text', 'an empty string' ),
    true,
    'isa_ok("", text, desc)',
    'an empty string isa text',
    ''
);

SELECT * FROM check_test(
    isa_ok( ''::text, 'text', 'an empty string' ),
    true,
    'isa_ok("", text, desc)',
    'an empty string isa text',
    ''
);

SELECT * FROM check_test(
    isa_ok( false, 'bool' ),
    true,
    'isa_ok(false, boolean)',
    'the value isa boolean',
    ''
);

SELECT * FROM check_test(
    isa_ok( NULL::boolean, 'bool' ),
    true,
    'isa_ok(NULL, boolean)',
    'the value isa boolean',
    ''
);

SELECT * FROM check_test(
    isa_ok( ARRAY[false], 'bool[]' ),
    true,
    'isa_ok(ARRAY, boolean[])',
    'the value isa boolean[]',
    ''
);

SELECT * FROM check_test(
    isa_ok( true, 'int[]' ),
    false,
    'isa_ok(bool, int[])',
    'the value isa integer[]',
    '    the value isn''t a "integer[]" it''s a "boolean"'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
