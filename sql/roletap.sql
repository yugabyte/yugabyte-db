\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(24);
--SELECT * FROM no_plan();

/****************************************************************************/
-- Test has_role() and hasnt_role().

SELECT * FROM check_test(
    has_role(current_role),
    true,
    'has_role(current role)',
    'Role ' || quote_ident(current_role) || ' should exist',
    ''
);
SELECT * FROM check_test(
    has_role(current_role, 'whatever'),
    true,
    'has_role(current role, desc)',
    'whatever',
    ''
);
SELECT * FROM check_test(
    has_role('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'has_role(nonexistent role)',
    'Role aoijaoisjfaoidfjaisjdfosjf should exist',
    ''
);
SELECT * FROM check_test(
    has_role('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'has_role(nonexistent role, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_role(current_role),
    false,
    'hasnt_role(current role)',
    'Role ' || quote_ident(current_role) || ' should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_role(current_role, 'whatever'),
    false,
    'hasnt_role(current role, desc)',
    'whatever',
    ''
);
SELECT * FROM check_test(
    hasnt_role('aoijaoisjfaoidfjaisjdfosjf'),
    true,
    'hasnt_role(nonexistent role)',
    'Role aoijaoisjfaoidfjaisjdfosjf should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_role('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    true,
    'hasnt_role(nonexistent role, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
