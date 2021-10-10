\unset ECHO
\i test/setup.sql

SELECT plan(372);
--SELECT * FROM no_plan();

SET client_min_messages = warning;
CREATE SCHEMA ha;
CREATE TABLE ha.sometab(id INT);
CREATE SEQUENCE ha.someseq;
CREATE SCHEMA "LOL";
CREATE TABLE "LOL"."ATable"("AColumn" INT);
CREATE SEQUENCE "LOL"."ASeq";
-- Include the new schemas in the path.
CREATE OR REPLACE FUNCTION set_search_path() returns setof text as $$
BEGIN
    IF pg_version_num() < 80200 THEN
        EXECUTE 'SET search_path = ha, "LOL", '
             || regexp_replace(current_setting('search_path'), '[$][^,]+,', '')
             || ', pg_catalog';
        RETURN;
    ELSE
        EXECUTE 'SET search_path = ha, "LOL", ' || current_setting('search_path') || ', pg_catalog';
    END IF;

END;
$$ language plpgsql;
SELECT * FROM set_search_path();
RESET client_min_messages;

/****************************************************************************/
-- Test table_privs_are().

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', current_user, _table_privs(), 'whatever' ),
    true,
    'table_privs_are(sch, tab, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'LOL', 'ATable', current_user, _table_privs(), 'whatever' ),
    true,
    'table_privs_are(LOL, ATable, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', current_user, _table_privs() ),
    true,
    'table_privs_are(sch, tab, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table ha.sometab' ,
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'LOL', 'ATable', current_user, _table_privs() ),
    true,
    'table_privs_are(LOL, ATable, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table "LOL"."ATable"' ,
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
    table_privs_are( 'ATable', current_user, _table_privs(), 'whatever' ),
    true,
    'table_privs_are(ATable, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'sometab', current_user, _table_privs() ),
    true,
    'table_privs_are(tab, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table sometab' ,
    ''
);

SELECT * FROM check_test(
    table_privs_are( 'ATable', current_user, _table_privs() ),
    true,
    'table_privs_are(ATable, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(_table_privs(), ', ') || ' on table "ATable"' ,
    ''
);

CREATE OR REPLACE FUNCTION run_extra_fails() RETURNS SETOF TEXT LANGUAGE plpgsql AS $$
DECLARE
    allowed_privs TEXT[];
    test_privs    TEXT[] := '{}';
    missing_privs TEXT[] := '{}';
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

-- Test default description with no permissions.
SELECT * FROM check_test(
    table_privs_are( 'ha', 'sometab', '__nonesuch', '{}'::text[] ),
    false,
    'table_privs_are(sch, tab, role, no privs)',
    'Role __nonesuch should be granted no privileges on table ha.sometab' ,
    '    Role __nonesuch does not exist'
);

SELECT * FROM check_test(
    table_privs_are( 'sometab', '__nonesuch', '{}'::text[] ),
    false,
    'table_privs_are(tab, role, no privs)',
    'Role __nonesuch should be granted no privileges on table sometab' ,
    '    Role __nonesuch does not exist'
);

/****************************************************************************/
-- Test database_privileges_are().

SELECT * FROM check_test(
    database_privs_are( current_database(), current_user, _db_privs(), 'whatever' ),
    true,
    'database_privs_are(db, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    database_privs_are( current_database(), current_user, _db_privs() ),
    true,
    'database_privs_are(db, role, privs, desc)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(_db_privs(), ', ') || ' on database ' || quote_ident( current_database() ),
    ''
);

-- Try nonexistent database.
SELECT * FROM check_test(
    database_privs_are( '__nonesuch', current_user, _db_privs(), 'whatever' ),
    false,
    'database_privs_are(non-db, role, privs, desc)',
    'whatever',
    '    Database __nonesuch does not exist'
);

-- Try nonexistent user.
SELECT * FROM check_test(
    database_privs_are( current_database(), '__noone', _db_privs(), 'whatever' ),
    false,
    'database_privs_are(db, non-role, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try another user.
SELECT * FROM check_test(
    database_privs_are( current_database(), '__someone_else', _db_privs(), 'whatever' ),
    false,
    'database_privs_are(db, ungranted, privs, desc)',
    'whatever',
    '    Missing privileges:
        CREATE'
);

-- Try a subset of privs.
SELECT * FROM check_test(
    database_privs_are(
        current_database(), current_user,
        CASE WHEN pg_version_num() < 80200 THEN ARRAY['CREATE'] ELSE ARRAY['CREATE', 'CONNECT'] END,
        'whatever'
    ),
    false,
    'database_privs_are(db, ungranted, privs, desc)',
    'whatever',
    '    Extra privileges:
        TEMPORARY'
);

-- Try testing default description for no permissions.
SELECT * FROM check_test(
    database_privs_are( current_database(), '__noone', '{}'::text[] ),
    false,
    'database_privs_are(db, non-role, no privs)',
    'Role __noone should be granted no privileges on database ' || quote_ident( current_database() ),
    '    Role __noone does not exist'
);

/****************************************************************************/
-- Test function_privs_are().
CREATE OR REPLACE FUNCTION public.foo(int, text) RETURNS VOID LANGUAGE SQL AS '';
CREATE OR REPLACE FUNCTION "LOL"."DoIt"(int, text) RETURNS VOID LANGUAGE SQL AS '';

SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(sch, func, args, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'LOL', 'DoIt', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(LOL, DoIt, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE']
    ),
    true,
    'function_privs_are(sch, func, args, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted EXECUTE on function public.foo(integer, text)'
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'LOL', 'DoIt', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE']
    ),
    true,
    'function_privs_are(LOL, DoIt, args, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted EXECUTE on function "LOL"."DoIt"(integer, text)'
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(func, args, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'DoIt', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(DoIt, args, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE']
    ),
    true,
    'function_privs_are(func, args, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted EXECUTE on function foo(integer, text)'
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'DoIt', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE']
    ),
    true,
    'function_privs_are(DoIt, args, role, privs)',
    'Role ' || quote_ident(current_user) || ' should be granted EXECUTE on function "DoIt"(integer, text)'
    ''
);

-- Try a nonexistent funtion.
SELECT * FROM check_test(
    function_privs_are(
        'public', '__nonesuch', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(sch, non-func, args, role, privs, desc)',
    'whatever',
    '    Function public.__nonesuch(integer, text) does not exist'
);

SELECT * FROM check_test(
    function_privs_are(
        '__nonesuch', ARRAY['integer', 'text'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(non-func, args, role, privs, desc)',
    'whatever',
    '    Function __nonesuch(integer, text) does not exist'
);

-- Try a nonexistent user.
SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        '__noone', ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(sch, func, args, noone, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        '__noone', ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(func, args, noone, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try an unprivileged user.
SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        '__someone_else', ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(sch, func, args, other, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        '__someone_else', ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'function_privs_are(func, args, other, privs, desc)',
    'whatever',
    ''
);

REVOKE EXECUTE ON FUNCTION public.foo(int, text) FROM PUBLIC;

-- Now fail.
SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        '__someone_else', ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(sch, func, args, unpriv, privs, desc)',
    'whatever',
    '    Missing privileges:
        EXECUTE'
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        '__someone_else', ARRAY['EXECUTE'], 'whatever'
    ),
    false,
    'function_privs_are(func, args, unpriv, privs, desc)',
    'whatever',
    '    Missing privileges:
        EXECUTE'
);

-- It should work for an empty array.
SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        '__someone_else', '{}'::text[], 'whatever'
    ),
    true,
    'function_privs_are(sch, func, args, unpriv, empty, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        '__someone_else', '{}'::text[]
    ),
    true,
    'function_privs_are(sch, func, args, unpriv, empty)',
    'Role __someone_else should be granted no privileges on function public.foo(integer, text)'
    ''
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        '__someone_else', '{}'::text[]
    ),
    true,
    'function_privs_are(func, args, unpriv, empty)',
    'Role __someone_else should be granted no privileges on function foo(integer, text)'
    ''
);

-- Empty for the current user should yeild an extra permission.
SELECT * FROM check_test(
    function_privs_are(
        'public', 'foo', ARRAY['integer', 'text'],
        current_user, '{}'::text[], 'whatever'
    ),
    false,
    'function_privs_are(sch, func, args, unpriv, privs, desc)',
    'whatever',
    '    Extra privileges:
        EXECUTE'
);

SELECT * FROM check_test(
    function_privs_are(
        'foo', ARRAY['integer', 'text'],
        current_user, '{}'::text[], 'whatever'
    ),
    false,
    'function_privs_are(func, args, unpriv, privs, desc)',
    'whatever',
    '    Extra privileges:
        EXECUTE'
);

-- Try a really long function signature.
CREATE OR REPLACE FUNCTION public.function_with_a_moderate_signature(
    anyelement, date, text[], boolean
) RETURNS VOID LANGUAGE SQL AS '';

SELECT * FROM check_test(
    function_privs_are(
        'public', 'function_with_a_moderate_signature',
        ARRAY['anyelement','date','text[]','boolean'],
        current_user, ARRAY['EXECUTE'], 'whatever'
    ),
    true,
    'long function signature',
    'whatever',
    ''
);

/****************************************************************************/
-- Test language_privilege_is().

SELECT * FROM check_test(
    language_privs_are( 'plpgsql', current_user, '{USAGE}', 'whatever' ),
    true,
    'language_privs_are(lang, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    language_privs_are( 'plpgsql', current_user, '{USAGE}' ),
    true,
    'language_privs_are(lang, role, privs, desc)',
    'Role ' || quote_ident(current_user) || ' should be granted USAGE on language plpgsql',
    ''
);

-- Try nonexistent language.
SELECT * FROM check_test(
    language_privs_are( '__nonesuch', current_user, '{USAGE}', 'whatever' ),
    false,
    'language_privs_are(non-lang, role, privs, desc)',
    'whatever',
    '    Language __nonesuch does not exist'
);

-- Try nonexistent user.
SELECT * FROM check_test(
    language_privs_are( 'plpgsql', '__noone', '{USAGE}', 'whatever' ),
    false,
    'language_privs_are(lang, non-role, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try another user.
REVOKE USAGE ON LANGUAGE plpgsql FROM public;
SELECT * FROM check_test(
    language_privs_are( 'plpgsql', '__someone_else', '{USAGE}', 'whatever' ),
    false,
    'language_privs_are(lang, ungranted, privs, desc)',
    'whatever',
    '    Missing privileges:
        USAGE'
);

-- Try testing default description for no permissions.
SELECT * FROM check_test(
    language_privs_are( 'plpgsql', '__someone_else', '{}'::text[] ),
    true,
    'language_privs_are(lang, role, no privs)',
    'Role __someone_else should be granted no privileges on language plpgsql',
    ''
);

/****************************************************************************/
-- Test schema_privileges_are().

SELECT * FROM check_test(
    schema_privs_are( current_schema(), current_user, ARRAY['CREATE', 'USAGE'], 'whatever' ),
    true,
    'schema_privs_are(schema, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    schema_privs_are( 'LOL', current_user, ARRAY['CREATE', 'USAGE'], 'whatever' ),
    true,
    'schema_privs_are(LOL, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    schema_privs_are( current_schema(), current_user, ARRAY['CREATE', 'USAGE'] ),
    true,
    'schema_privs_are(schema, role, privs, desc)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(ARRAY['CREATE', 'USAGE'], ', ') || ' on schema ' || current_schema(),
    ''
);

SELECT * FROM check_test(
    schema_privs_are( 'LOL', current_user, ARRAY['CREATE', 'USAGE'] ),
    true,
    'schema_privs_are(LOL, role, privs, desc)',
    'Role ' || quote_ident(current_user) || ' should be granted '
         || array_to_string(ARRAY['CREATE', 'USAGE'], ', ') || ' on schema "LOL"',
    ''
);

-- Try nonexistent schema.
SELECT * FROM check_test(
    schema_privs_are( '__nonesuch', current_user, ARRAY['CREATE', 'USAGE'], 'whatever' ),
    false,
    'schema_privs_are(non-schema, role, privs, desc)',
    'whatever',
    '    Schema __nonesuch does not exist'
);

-- Try nonexistent user.
SELECT * FROM check_test(
    schema_privs_are( current_schema(), '__noone', ARRAY['CREATE', 'USAGE'], 'whatever' ),
    false,
    'schema_privs_are(schema, non-role, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try another user.
SELECT * FROM check_test(
    schema_privs_are( current_schema(), '__someone_else', ARRAY['CREATE', 'USAGE'], 'whatever' ),
    false,
    'schema_privs_are(schema, ungranted, privs, desc)',
    'whatever',
    '    Missing privileges:
        CREATE
        USAGE'
);

-- Try a subset of privs.
SELECT * FROM check_test(
    schema_privs_are(
        current_schema(), current_user, ARRAY['CREATE'],
        'whatever'
    ),
    false,
    'schema_privs_are(schema, ungranted, privs, desc)',
    'whatever',
    '    Extra privileges:
        USAGE'
);

-- Try testing default description for no permissions.
SELECT * FROM check_test(
    schema_privs_are( current_schema(), '__noone', '{}'::text[] ),
    false,
    'schema_privs_are(schema, non-role, no privs)',
    'Role __noone should be granted no privileges on schema ' || current_schema(),
    '    Role __noone does not exist'
);

/****************************************************************************/
-- Test tablespace_privilege_is().

SELECT * FROM check_test(
    tablespace_privs_are( 'pg_default', current_user, '{CREATE}', 'whatever' ),
    true,
    'tablespace_privs_are(tablespace, role, privs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tablespace_privs_are( 'pg_default', current_user, '{CREATE}' ),
    true,
    'tablespace_privs_are(tablespace, role, privs, desc)',
    'Role ' || quote_ident(current_user) || ' should be granted CREATE on tablespace pg_default',
    ''
);

-- Try nonexistent tablespace.
SELECT * FROM check_test(
    tablespace_privs_are( '__nonesuch', current_user, '{CREATE}', 'whatever' ),
    false,
    'tablespace_privs_are(non-tablespace, role, privs, desc)',
    'whatever',
    '    Tablespace __nonesuch does not exist'
);

-- Try nonexistent user.
SELECT * FROM check_test(
    tablespace_privs_are( 'pg_default', '__noone', '{CREATE}', 'whatever' ),
    false,
    'tablespace_privs_are(tablespace, non-role, privs, desc)',
    'whatever',
    '    Role __noone does not exist'
);

-- Try another user.
REVOKE CREATE ON TABLESPACE pg_default FROM public;
SELECT * FROM check_test(
    tablespace_privs_are( 'pg_default', '__someone_else', '{CREATE}', 'whatever' ),
    false,
    'tablespace_privs_are(tablespace, ungranted, privs, desc)',
    'whatever',
    '    Missing privileges:
        CREATE'
);

-- Try testing default description for no permissions.
SELECT * FROM check_test(
    tablespace_privs_are( 'pg_default', '__someone_else', '{}'::text[] ),
    true,
    'tablespace_privs_are(tablespace, role, no privs)',
    'Role __someone_else should be granted no privileges on tablespace pg_default',
    ''
);

/****************************************************************************/
-- Test sequence_privilege_is().

CREATE FUNCTION test_sequence() RETURNS SETOF TEXT AS $$
DECLARE
    allowed_privs TEXT[];
    test_privs    TEXT[];
    missing_privs TEXT[];
    tap           record;
    last_index    INTEGER;
BEGIN
    IF pg_version_num() >= 90000 THEN
        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', current_user, ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'LOL', 'ASeq', current_user, ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'sequence_privs_are(LOL, ASeq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', current_user, ARRAY['USAGE', 'SELECT', 'UPDATE'] ),
            true,
            'sequence_privs_are(sch, seq, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['USAGE', 'SELECT', 'UPDATE'], ', ')
                || ' on sequence ha.someseq' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'LOL', 'ASeq', current_user, ARRAY['USAGE', 'SELECT', 'UPDATE'] ),
            true,
            'sequence_privs_are(sch, seq, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['USAGE', 'SELECT', 'UPDATE'], ', ')
                || ' on sequence "LOL"."ASeq"' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'someseq', current_user, ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'sequence_privs_are(seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ASeq', current_user, ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'sequence_privs_are(ASeq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'someseq', current_user, ARRAY['USAGE', 'SELECT', 'UPDATE'] ),
            true,
            'sequence_privs_are(seq, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['USAGE', 'SELECT', 'UPDATE'], ', ')
                || ' on sequence someseq' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ASeq', current_user, ARRAY['USAGE', 'SELECT', 'UPDATE'] ),
            true,
            'sequence_privs_are(seq, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['USAGE', 'SELECT', 'UPDATE'], ', ')
                || ' on sequence "ASeq"' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test sequence failure.
        allowed_privs := ARRAY['USAGE', 'SELECT', 'UPDATE'];
        last_index    := array_upper(allowed_privs, 1);
        FOR i IN 1..last_index - 2 LOOP
            test_privs := test_privs || allowed_privs[i];
        END LOOP;
        FOR i IN last_index - 1..last_index LOOP
            missing_privs := missing_privs || allowed_privs[i];
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', current_user, test_privs, 'whatever' ),
            false,
            'sequence_privs_are(sch, seq, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'someseq', current_user, test_privs, 'whatever' ),
            false,
            'sequence_privs_are(seq, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', '__someone_else', ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'sequence_privs_are(sch, seq, other, privs, desc)',
            'whatever',
            '    Missing privileges:
        ' || array_to_string(ARRAY['SELECT', 'UPDATE', 'USAGE'], E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Grant them some permission.
        GRANT SELECT, UPDATE ON ha.someseq TO __someone_else;
        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', '__someone_else', ARRAY[
                'SELECT', 'UPDATE'
            ], 'whatever'),
            true,
            'sequence_privs_are(sch, seq, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent sequence.
        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'nonesuch', current_user, ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            '    Sequence ha.nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', '__nonesuch', ARRAY[
                'USAGE', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'ha', 'someseq', '__nonesuch', '{}'::text[] ),
            false,
            'sequence_privs_are(sch, seq, role, no privs)',
            'Role __nonesuch should be granted no privileges on sequence ha.someseq' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            sequence_privs_are( 'someseq', '__nonesuch', '{}'::text[] ),
            false,
            'sequence_privs_are(seq, role, no privs)',
            'Role __nonesuch should be granted no privileges on sequence someseq' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(LOL, ASeq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(sch, seq, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(sch, seq, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(ASeq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(seq, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(seq, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(sch, seq, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(seq, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(sch, seq, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Grant them some permission.
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'sequence_privs_are(sch, seq, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent sequence.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(sch, seq, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(sch, seq, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'sequence_privs_are(seq, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    END IF;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_sequence();

/****************************************************************************/
-- Test any_column_privs_are().
CREATE FUNCTION test_anycols() RETURNS SETOF TEXT AS $$
DECLARE
    allowed_privs TEXT[];
    test_privs    TEXT[];
    missing_privs TEXT[];
    tap           record;
    last_index    INTEGER;
BEGIN
    IF pg_version_num() >= 80400 THEN
        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'any_column_privs_are(sch, tab, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on any column in ha.sometab' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'sometab', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'any_column_privs_are(tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'sometab', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'any_column_privs_are(tab, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on any column in sometab' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test table failure.
        allowed_privs := ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'];
        last_index    := array_upper(allowed_privs, 1);
        FOR i IN 1..last_index - 2 LOOP
            test_privs := test_privs || allowed_privs[i];
        END LOOP;
        FOR i IN last_index - 1..last_index LOOP
            missing_privs := missing_privs || allowed_privs[i];
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', current_user, test_privs, 'whatever' ),
            false,
            'any_column_privs_are(sch, tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'sometab', current_user, test_privs, 'whatever' ),
            false,
            'any_column_privs_are(tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', '__someone_else', ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'any_column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            '    Missing privileges:
        ' || array_to_string(ARRAY['REFERENCES'], E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', '__someone_else', ARRAY[
                'SELECT', 'INSERT', 'UPDATE'
            ], 'whatever'),
            true,
            'any_column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent table.
        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'nonesuch', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            '    Table ha.nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', '__nonesuch', ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'ha', 'sometab', '__nonesuch', '{}'::text[] ),
            false,
            'any_column_privs_are(sch, tab, role, no privs)',
            'Role __nonesuch should be granted no privileges on any column in ha.sometab' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            any_column_privs_are( 'sometab', '__nonesuch', '{}'::text[] ),
            false,
            'any_column_privs_are(tab, role, no privs)',
            'Role __nonesuch should be granted no privileges on any column in sometab' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'any_column_privs_are(sch, tab, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'any_column_privs_are(tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'any_column_privs_are(tab, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(sch, tab, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(tab, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'any_column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent table.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(sch, tab, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'any_column_privs_are(tab, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;
    END IF;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_anycols();

/****************************************************************************/
-- Test column_privs_are().

CREATE FUNCTION test_cols() RETURNS SETOF TEXT AS $$
DECLARE
    allowed_privs TEXT[];
    test_privs    TEXT[];
    missing_privs TEXT[];
    tap           record;
    last_index    INTEGER;
BEGIN
    IF pg_version_num() >= 80400 THEN
        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'column_privs_are(sch, tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'LOL', 'ATable', 'AColumn', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'column_privs_are(LOL, ATable, AColumn, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'column_privs_are(sch, tab, col, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on column ha.sometab.id' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'LOL', 'ATable', 'AColumn', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'column_privs_are(LOL, ATable, AColumn, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on column "LOL"."ATable"."AColumn"' ,
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'sometab', 'id', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'column_privs_are(tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ATable', 'AColumn', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            true,
            'column_privs_are(tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'sometab', 'id', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'column_privs_are(tab, col, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on column sometab.id' ,
                ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ATable', 'AColumn', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ] ),
            true,
            'column_privs_are(tab, col, role, privs)',
            'Role ' || quote_ident(current_user) || ' should be granted '
                || array_to_string(ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'], ', ')
                || ' on column "ATable"."AColumn"' ,
                ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test table failure.
        allowed_privs := ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'];
        last_index    := array_upper(allowed_privs, 1);
        FOR i IN 1..last_index - 2 LOOP
            test_privs := test_privs || allowed_privs[i];
        END LOOP;
        FOR i IN last_index - 1..last_index LOOP
            missing_privs := missing_privs || allowed_privs[i];
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', current_user, test_privs, 'whatever' ),
            false,
            'column_privs_are(sch, tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'sometab', 'id', current_user, test_privs, 'whatever' ),
            false,
            'column_privs_are(tab, role, some privs, desc)',
            'whatever',
            '    Extra privileges:
        ' || array_to_string(missing_privs, E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', '__someone_else', ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            '    Missing privileges:
        ' || array_to_string(ARRAY['REFERENCES'], E'\n        ')
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Grant them some permission.
        EXECUTE 'GRANT SELECT, INSERT, UPDATE (id) ON ha.sometab TO __someone_else;';

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', '__someone_else', ARRAY[
                'SELECT', 'INSERT', 'UPDATE'
            ], 'whatever'),
            true,
            'column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent table.
        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'nonesuch', 'id', current_user, ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            '    Table ha.nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', '__nonesuch', ARRAY[
                'INSERT', 'REFERENCES', 'SELECT', 'UPDATE'
            ], 'whatever' ),
            false,
            'column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'ha', 'sometab', 'id', '__nonesuch', '{}'::text[] ),
            false,
            'column_privs_are(sch, tab, role, no privs)',
            'Role __nonesuch should be granted no privileges on column ha.sometab.id' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            column_privs_are( 'sometab', 'id', '__nonesuch', '{}'::text[] ),
            false,
            'column_privs_are(tab, role, no privs)',
            'Role __nonesuch should be granted no privileges on column sometab.id' ,
            '    Role __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    ELSE
        -- Fake it.
       FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(sch, tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

       FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(LOL, ATable, AColumn, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(sch, tab, col, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(LOL, ATable, AColumn, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(tab, col, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(tab, col, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(tab, col, role, privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(sch, tab, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(tab, role, some privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'column_privs_are(sch, tab, other, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent table.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try a non-existent user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(sch, tab, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Test default description with no permissions.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(sch, tab, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'column_privs_are(tab, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    END IF;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_cols();

/****************************************************************************/
-- Test fdw_privs_are().

CREATE FUNCTION test_fdw() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 80400 THEN
        EXECUTE 'CREATE FOREIGN DATA WRAPPER dummy;';
        EXECUTE 'CREATE FOREIGN DATA WRAPPER "SomeFDW";';

        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'dummy', current_user, '{USAGE}', 'whatever' ),
            true,
            'fdw_privs_are(fdw, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'SomeFDW', current_user, '{USAGE}', 'whatever' ),
            true,
            'fdw_privs_are(SomeFDW, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'dummy', current_user, '{USAGE}' ),
            true,
            'fdw_privs_are(fdw, role, privs, desc)',
            'Role ' || quote_ident(current_user) || ' should be granted USAGE on FDW dummy',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'SomeFDW', current_user, '{USAGE}' ),
            true,
            'fdw_privs_are(SomeFDW, role, privs, desc)',
            'Role ' || quote_ident(current_user) || ' should be granted USAGE on FDW "SomeFDW"',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try nonexistent fdw.
        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( '__nonesuch', current_user, '{USAGE}', 'whatever' ),
            false,
            'fdw_privs_are(non-fdw, role, privs, desc)',
            'whatever',
            '    FDW __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try nonexistent user.
        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'dummy', '__noone', '{USAGE}', 'whatever' ),
            false,
            'fdw_privs_are(fdw, non-role, privs, desc)',
            'whatever',
            '    Role __noone does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try another user.
        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'dummy', '__someone_else', '{USAGE}', 'whatever' ),
            false,
            'fdw_privs_are(fdw, ungranted, privs, desc)',
            'whatever',
            '    Missing privileges:
        USAGE'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try testing default description for no permissions.
        FOR tap IN SELECT * FROM check_test(
            fdw_privs_are( 'dummy', '__someone_else', '{}'::text[] ),
            true,
            'fdw_privs_are(fdw, role, no privs)',
            'Role __someone_else should be granted no privileges on FDW dummy',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'fdw_privs_are(fdw, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'fdw_privs_are(SomeFDW, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'fdw_privs_are(fdw, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'fdw_privs_are(SomeFDW, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try nonexistent fdw.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'fdw_privs_are(non-fdw, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try nonexistent user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'fdw_privs_are(fdw, non-role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try another user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'fdw_privs_are(fdw, ungranted, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try testing default description for no permissions.
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'fdw_privs_are(fdw, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;

SELECT * FROM test_fdw();

CREATE FUNCTION test_server() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 80400 THEN
        EXECUTE 'CREATE SERVER foo FOREIGN DATA WRAPPER dummy;';
        EXECUTE 'CREATE SERVER "SomeServer" FOREIGN DATA WRAPPER "SomeFDW";';

        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'foo', current_user, '{USAGE}', 'whatever' ),
            true,
            'server_privs_are(server, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'SomeServer', current_user, '{USAGE}', 'whatever' ),
            true,
            'server_privs_are(SomeServer, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'foo', current_user, '{USAGE}' ),
            true,
            'server_privs_are(server, role, privs, desc)',
            'Role ' || quote_ident(current_user) || ' should be granted USAGE on server foo',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'SomeServer', current_user, '{USAGE}' ),
            true,
            'server_privs_are(SomeServer, role, privs, desc)',
            'Role ' || quote_ident(current_user) || ' should be granted USAGE on server "SomeServer"',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try nonexistent server.
        FOR tap IN SELECT * FROM check_test(
            server_privs_are( '__nonesuch', current_user, '{USAGE}', 'whatever' ),
            false,
            'server_privs_are(non-server, role, privs, desc)',
            'whatever',
            '    Server __nonesuch does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try nonexistent user.
        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'foo', '__noone', '{USAGE}', 'whatever' ),
            false,
            'server_privs_are(server, non-role, privs, desc)',
            'whatever',
            '    Role __noone does not exist'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try another user.
        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'foo', '__someone_else', '{USAGE}', 'whatever' ),
            false,
            'server_privs_are(server, ungranted, privs, desc)',
            'whatever',
            '    Missing privileges:
        USAGE'
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try testing default description for no permissions.
        FOR tap IN SELECT * FROM check_test(
            server_privs_are( 'foo', '__someone_else', '{}'::text[] ),
            true,
            'server_privs_are(server, role, no privs)',
            'Role __someone_else should be granted no privileges on server foo',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    ELSE
        -- Fake it with pass() and fail().
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'server_privs_are(server, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'server_privs_are(SomeServer, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'server_privs_are(server, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'server_privs_are(SomeServer, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try nonexistent server.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'server_privs_are(non-server, role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try nonexistent user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'server_privs_are(server, non-role, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;


        -- Try another user.
        FOR tap IN SELECT * FROM check_test(
            fail('whatever'),
            false,
            'server_privs_are(server, ungranted, privs, desc)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

        -- Try testing default description for no permissions.
        FOR tap IN SELECT * FROM check_test(
            pass('whatever'),
            true,
            'server_privs_are(server, role, no privs)',
            'whatever',
            ''
        ) AS b LOOP RETURN NEXT tap.b; END LOOP;

    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;

SELECT * FROM test_server();


/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
