\unset ECHO
\i test/setup.sql

SELECT plan(39);
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
-- Test roles_are().

CREATE FUNCTION ___myroles(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY( SELECT rolname FROM pg_catalog.pg_roles WHERE rolname <> $1 ), '{}'::name[]);;
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    roles_are( ___myroles(''), 'whatever' ),
    true,
    'roles_are(roles, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    roles_are( ___myroles('') ),
    true,
    'roles_are(roles)',
    'There should be the correct roles',
    ''
);

SELECT * FROM check_test(
    roles_are( array_append(___myroles(''), '__howdy__'), 'whatever' ),
    false,
    'roles_are(roles, desc) missing',
    'whatever',
    '    Missing roles:
        __howdy__'
);

SELECT * FROM check_test(
    roles_are( ___myroles(current_role), 'whatever' ),
    false,
    'roles_are(roles, desc) extras',
    'whatever',
    '    Extra roles:
        ' || quote_ident(current_role)
);

SELECT * FROM check_test(
    roles_are( array_append(___myroles(current_role), '__howdy__'), 'whatever' ),
    false,
    'roles_are(roles, desc) missing and extras',
    'whatever',
    '    Extra roles:
        ' || quote_ident(current_role) || '
    Missing roles:
        __howdy__'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
