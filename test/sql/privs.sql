\unset ECHO
\i test/setup.sql

SELECT plan(48);
--SELECT * FROM no_plan();

SET client_min_messages = warning;
CREATE SCHEMA ha;
CREATE TABLE ha.sometab(id INT);
SET search_path = ha,public,pg_catalog;
RESET client_min_messages;

/****************************************************************************/
-- Test table_privilege_is().

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', current_user, _table_privs(), 'whatever' ),
    true,
    'table_privs_are(sch, tab, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', current_user, _table_privs() ),
    true,
    'table_privs_are(sch, tab, role, privs)',
    'Role ' || current_user || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table ha.sometab' ,
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'sometab', current_user, _table_privs(), 'whatever' ),
    true,
    'table_privs_are(tab, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'sometab', current_user, _table_privs() ),
    true,
    'table_privs_are(tab, role, privs)',
    'Role ' || current_user || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table sometab' ,
    ''
);

CREATE OR REPLACE FUNCTION run_extra_fails() RETURNS SETOF TEXT LANGUAGE plpgsql AS $$
DECLARE
    allowed_privs TEXT[];
    test_privs    TEXT[];
    missing_privs TEXT[];
    tap           record;
    last_index    INTEGER;
BEGIN
    -- Test table failure.
    allowed_privs := _table_privs();
    last_index    := array_upper(allowed_privs, 1);
    FOR i IN 1..last_index - 2 LOOP
        test_privs := test_privs || allowed_privs[i];
    END LOOP;
    FOR i IN last_index - 1..last_index LOOP
        missing_privs := missing_privs || allowed_privs[i];
    END LOOP;

    FOR tap IN SELECT * FROM check_test(
        table_privs_are( 'ha', 'sometab', current_user, test_privs, 'whatever' ),
            false,
            'table_privs_are(sch, tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
    ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    FOR tap IN SELECT * FROM check_test(
            table_privs_are( 'sometab', current_user, test_privs, 'whatever' ),
            false,
            'table_privs_are(tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
    ) AS b LOOP RETURN NEXT tap.b; END LOOP;
END;
$$;

SELECT * FROM run_extra_fails();

-- Create another role.
CREATE USER __someone_else;

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', '__someone_else', _table_privs(), 'whatever' ),
    false,
    'table_privs_are(sch, tab, other, privs, desc)',
    'whatever',
    '    Missing privileges:
        ' || array_to_string(_table_privs(), E'\n        ')
);

-- Grant them some permission.
GRANT SELECT, INSERT, UPDATE, DELETE ON ha.sometab TO __someone_else;

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', '__someone_else', ARRAY[
        'SELECT', 'INSERT', 'UPDATE', 'DELETE'
    ], 'whatever'),
    true,
    'table_privs_are(sch, tab, other, privs, desc)',
    'whatever',
    ''
);

-- Try a non-existent table.
SELECT * FROM check_test(
    table_privs_are( 'ha', 'nonesuch', current_user, _table_privs(), 'whatever' ),
    false,
    'table_privs_are(sch, tab, role, privs, desc)',
    'whatever',
    '    Table ha.nonesuch does not exist'
);

-- Try a non-existent user.
SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', '__nonesuch', _table_privs(), 'whatever' ),
    false,
    'table_privs_are(sch, tab, role, privs, desc)',
    'whatever',
    '    Role __nonesuch does not exist'
);

/****************************************************************************/
-- Test db_privilege_is().

SELECT * FROM check_test(
    db_privs_are( current_database(), current_user, _db_privs(), 'whatever' ),
    true,
    'db_privs_are(db, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    db_privs_are( current_database(), current_user, _db_privs() ),
    true,
    'db_privs_are(db, role, privs, desc)',
    'Role ' || current_user || ' should be granted '
         || array_to_string(_db_privs(), ', ') || ' on database ' || current_database(),
    ''
);

-- Try nonexistent database.
SELECT * FROM check_test(
    db_privs_are( '__nonesuch', current_user, _db_privs(), 'whatever' ),
    false,
    'db_privs_are(non-db, role, privs, desc)',
    'whatever',
    '    Database __nonesuch does not exist'
);

-- Try nonexistent user.
SELECT * FROM check_test(
    db_privs_are( current_database(), '__noone', _db_privs(), 'whatever' ),
    false,
    'db_privs_are(db, non-role, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try another user.
SELECT * FROM check_test(
    db_privs_are( current_database(), '__someone_else', _db_privs(), 'whatever' ),
    false,
    'db_privs_are(db, ungranted, privs, desc)',
    'whatever',
    '    Missing privileges:
        CREATE'
);

-- Try a subset of privs.
SELECT * FROM check_test(
    db_privs_are(
        current_database(), current_user,
        CASE WHEN pg_version_num() < 80200 THEN ARRAY['CREATE'] ELSE ARRAY['CREATE', 'CONNECT'] END,
        'whatever'
    ),
    false,
    'db_privs_are(db, ungranted, privs, desc)',
    'whatever',
    '    Extra privileges:
        TEMPORARY'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
