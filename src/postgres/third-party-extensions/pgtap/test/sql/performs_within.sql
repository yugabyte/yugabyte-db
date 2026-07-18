\unset ECHO
\i test/setup.sql

SELECT plan(24);
--SELECT * FROM no_plan();

/****************************************************************************/
-- Test performs_within().
SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 500, 500, 1, 'whatever' ),
    true,
    'simple select',
    'whatever',
    ''
);

SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 500, 500, 1 ),
    true,
    'simple select no desc',
    'Should run within 500 +/- 500 ms',
    ''
);

SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 99.99, 99.99 ),
    true,
    'simple select numeric',
    'Should run within 99.99 +/- 99.99 ms',
    ''
);

PREPARE mytest AS SELECT TRUE;
SELECT * FROM check_test(
    performs_within( 'mytest', 500, 500 ),
    true,
    'simple prepare',
    'Should run within 500 +/- 500 ms',
    ''
);

SELECT * FROM check_test(
    performs_within( 'EXECUTE mytest', 500, 500 ),
    true,
    'simple execute',
    'Should run within 500 +/- 500 ms',
    ''
);

SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 0, 0, 'whatever' ),
    false,
    'simple select fail',
    'whatever',
    ' average runtime: [[:digit:]]+([.][[:digit:]]+)? ms' ||
    E'\n' || E' desired average: 0 \\+/- 0 ms',
    true
);

SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 0, 0 ),
    false,
    'simple select no desc fail',
    'Should run within 0 +/- 0 ms',
    ' average runtime: [[:digit:]]+([.][[:digit:]]+)? ms' ||
    E'\n' || E' desired average: 0 \\+/- 0 ms',
    true
);

SELECT * FROM check_test(
    performs_within( 'SELECT TRUE', 0.0, 0.0 ),
    false,
    'simple select no desc numeric fail',
    'Should run within 0.0 +/- 0.0 ms',
    ' average runtime: [[:digit:]]+([.][[:digit:]]+)? ms' ||
    E'\n' || E' desired average: 0.0 \\+/- 0.0 ms',
    true
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
