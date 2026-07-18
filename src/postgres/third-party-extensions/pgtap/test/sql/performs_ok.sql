\unset ECHO
\i test/setup.sql

SELECT plan(24);
--SELECT * FROM no_plan();

/****************************************************************************/
-- Test performs_ok().
SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 500, 'whatever' ),
    true,
    'simple select',
    'whatever',
    ''
);

SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 500 ),
    true,
    'simple select no desc',
    'Should run in less than 500 ms',
    ''
);

SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 199.99 ),
    true,
    'simple select numeric',
    'Should run in less than 199.99 ms',
    ''
);

PREPARE mytest AS SELECT TRUE;
SELECT * FROM check_test(
    performs_ok( 'mytest', 100 ),
    true,
    'simple prepare',
    'Should run in less than 100 ms',
    ''
);

SELECT * FROM check_test(
    performs_ok( 'EXECUTE mytest', 100 ),
    true,
    'simple execute',
    'Should run in less than 100 ms',
    ''
);

SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 0, 'whatever' ),
    false,
    'simple select fail',
    'whatever',
    '      runtime: [[:digit:]]+([.][[:digit:]]+)? ms
      exceeds: 0 ms',
    true
);

SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 0 ),
    false,
    'simple select no desc fail',
    'Should run in less than 0 ms',
    '      runtime: [[:digit:]]+([.][[:digit:]]+)? ms
      exceeds: 0 ms',
    true
);

SELECT * FROM check_test(
    performs_ok( 'SELECT TRUE', 0.00 ),
    false,
    'simple select no desc numeric fail',
    'Should run in less than 0.00 ms',
    '      runtime: [[:digit:]]+([.][[:digit:]]+)? ms
      exceeds: 0.00 ms',
    true
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
