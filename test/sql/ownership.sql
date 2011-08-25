\unset ECHO
\i test/setup.sql

SELECT plan(12);
--SELECT * FROM no_plan();

/****************************************************************************/
SELECT * From check_test(
    db_owner_is(current_database(), current_user, 'mumble'),
	true,
    'db_owner_is(db, user, desc)',
    'mumble',
    ''
);

SELECT * From check_test(
    db_owner_is(current_database(), current_user),
	true,
    'db_owner_is(db, user)',
    'Database ' || quote_ident(current_database()) || ' should be owned by ' || current_user,
    ''
);

SELECT * From check_test(
    db_owner_is('__not__' || current_database(), current_user, 'mumble'),
	false,
    'db_owner_is(non-db, user)',
    'mumble',
    '    Database __not__' || current_database() || ' does not exist'
);

SELECT * From check_test(
    db_owner_is(current_database(), '__not__' || current_user, 'mumble'),
	false,
    'db_owner_is(db, non-user)',
    'mumble',
    '        have: ' || current_user || '
        want: __not__' || current_user
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;

