\unset ECHO
\i test/setup.sql

SELECT plan(90);
--SELECT * FROM no_plan();

/****************************************************************************/
-- Test has_user() and hasnt_user().

SELECT * FROM check_test(
    has_user(current_user),
    true,
    'has_user(current user)',
    'User ' || quote_ident(current_user) || ' should exist',
    ''
);
SELECT * FROM check_test(
    has_user(current_user, 'whatever'),
    true,
    'has_user(current user, desc)',
    'whatever',
    ''
);
SELECT * FROM check_test(
    has_user('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'has_user(nonexistent user)',
    'User aoijaoisjfaoidfjaisjdfosjf should exist',
    ''
);
SELECT * FROM check_test(
    has_user('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'has_user(nonexistent user, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_user(current_user),
    false,
    'hasnt_user(current user)',
    'User ' || quote_ident(current_user) || ' should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_user(current_user, 'whatever'),
    false,
    'hasnt_user(current user, desc)',
    'whatever',
    ''
);
SELECT * FROM check_test(
    hasnt_user('aoijaoisjfaoidfjaisjdfosjf'),
    true,
    'hasnt_user(nonexistent user)',
    'User aoijaoisjfaoidfjaisjdfosjf should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_user('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    true,
    'hasnt_user(nonexistent user, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test is_superuser() and isnt_superuser().
SELECT * FROM check_test(
    is_superuser('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'is_superuser(nonexistent user, desc)',
    'desc',
    '    User aoijaoisjfaoidfjaisjdfosjf does not exist'
);
SELECT * FROM check_test(
    is_superuser('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'is_superuser(nonexistent user)',
    'User aoijaoisjfaoidfjaisjdfosjf should be a super user',
    '    User aoijaoisjfaoidfjaisjdfosjf does not exist'
);

SELECT * FROM check_test(
    isnt_superuser('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'isnt_superuser(nonexistent user, desc)',
    'desc',
    '    User aoijaoisjfaoidfjaisjdfosjf does not exist'
);
SELECT * FROM check_test(
    isnt_superuser('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'isnt_superuser(nonexistent user)',
    'User aoijaoisjfaoidfjaisjdfosjf should not be a super user',
    '    User aoijaoisjfaoidfjaisjdfosjf does not exist'
);

SELECT * FROM check_test(
    is_superuser(current_user),
    true,
    'is_superuser( current user )',
    'User ' || quote_ident(current_user) || ' should be a super user',
    ''
);

SELECT * FROM check_test(
    is_superuser(current_user, 'whatever'),
    true,
    'is_superuser( current user, desc )',
    'whatever',
    ''
);

/****************************************************************************/
-- Test has_group() and hasnt_group().
CREATE GROUP meanies;

SELECT * FROM check_test(
    has_group('meanies'),
    true,
    'has_group(group)',
    'Group ' || quote_ident('meanies') || ' should exist',
    ''
);

SELECT * FROM check_test(
    has_group('meanies', 'whatever'),
    true,
    'has_group(group, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_group('aoijaoisjfaoidfjaisjdfosjf'),
    false,
    'has_group(nonexistent group)',
    'Group aoijaoisjfaoidfjaisjdfosjf should exist',
    ''
);

SELECT * FROM check_test(
    has_group('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    false,
    'has_group(nonexistent group, desc)',
    'desc',
    ''
);

SELECT * FROM check_test(
    hasnt_group('meanies'),
    false,
    'hasnt_group(group)',
    'Group ' || quote_ident('meanies') || ' should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_group('meanies', 'whatever'),
    false,
    'hasnt_group(group, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_group('aoijaoisjfaoidfjaisjdfosjf'),
    true,
    'hasnt_group(nonexistent group)',
    'Group aoijaoisjfaoidfjaisjdfosjf should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_group('aoijaoisjfaoidfjaisjdfosjf', 'desc'),
    true,
    'hasnt_group(nonexistent group, desc)',
    'desc',
    ''
);

/****************************************************************************/
-- Test isnt_member_of().
SELECT * FROM check_test(
    isnt_member_of('meanies', ARRAY[current_user], 'whatever' ),
    true,
    'isnt_member_of(meanies, [current_user], desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_member_of('meanies', ARRAY[current_user] ),
    true,
    'isnt_member_of(meanies, [current_user])',
    'Should not have members of role meanies',
    ''
);

SELECT * FROM check_test(
    isnt_member_of('meanies', current_user, 'whatever' ),
    true,
    'isnt_member_of(meanies, current_user, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_member_of('meanies', current_user ),
    true,
    'isnt_member_of(meanies, current_user)',
    'Should not have members of role meanies',
    ''
);

/****************************************************************************/
-- Test is_member_of().
CREATE OR REPLACE FUNCTION addmember() RETURNS SETOF TEXT AS $$
BEGIN
    EXECUTE 'ALTER GROUP meanies ADD USER ' || quote_ident(current_user);
    RETURN;
END;
$$ LANGUAGE PLPGSQL;    

SELECT * FROM addmember();
SELECT * FROM check_test(
    is_member_of('meanies', ARRAY[current_user], 'whatever' ),
    true,
    'is_member_of(meanies, [current_user], desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_member_of('meanies', ARRAY[current_user] ),
    true,
    'is_member_of(meanies, [current_user])',
    'Should have members of role meanies',
    ''
);

SELECT * FROM check_test(
    is_member_of('meanies', current_user, 'whatever' ),
    true,
    'is_member_of(meanies, current_user, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_member_of('meanies', current_user ),
    true,
    'is_member_of(meanies, current_user)',
    'Should have members of role meanies',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
