\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(13);

/****************************************************************************/
-- Test todo tests.
\echo ok 1 - todo fail
\echo ok 2 - todo pass
SELECT * FROM todo('just because', 2 );
SELECT is(
    fail('This is a todo test' ) || '
'
      || pass('This is a todo test that unexpectedly passes' ),
    'not ok 1 - This is a todo test # TODO just because
# Failed (TODO) test 1: "This is a todo test"
ok 2 - This is a todo test that unexpectedly passes # TODO just because',
   'TODO tests should display properly'
);
UPDATE __tresults__ SET ok = true, aok = true WHERE numb IN( 2 );

/****************************************************************************/
-- Test skipping tests.
SELECT * FROM check_test(
    skip('Just because'),
    true,
    'simple skip',
    'SKIP: Just because',
    ''
);

SELECT * FROM check_test(
    skip('Just because', 1),
    true,
    'skip with num',
    'SKIP: Just because',
    ''
);

\echo ok 10 Skip multiple
\echo ok 11 Skip multiple
\echo ok 12 Skip multiple
SELECT is(
   skip( 'Whatever', 3 ),
   'ok 10 - SKIP: Whatever
ok 11 - SKIP: Whatever
ok 12 - SKIP: Whatever',
   'We should get the proper output for multiple skips'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
