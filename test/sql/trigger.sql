\unset ECHO
\i test/setup.sql

SELECT plan(84);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.users(
    nick  text NOT NULL PRIMARY KEY,
    pass  text NOT NULL
);
CREATE FUNCTION public.hash_pass() RETURNS TRIGGER AS '
BEGIN
    NEW.pass := MD5( NEW.pass );
    RETURN NEW;
END;
' LANGUAGE plpgsql;

CREATE TRIGGER set_users_pass
BEFORE INSERT ON public.users
FOR EACH ROW EXECUTE PROCEDURE hash_pass();

CREATE TRIGGER upd_users_pass
BEFORE UPDATE ON public.users
FOR EACH ROW EXECUTE PROCEDURE hash_pass();
RESET client_min_messages;

/****************************************************************************/
-- Test has_trigger() and hasnt_trigger().

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'set_users_pass', 'whatever' ),
    true,
    'has_trigger(schema, table, trigger, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'set_users_pass'::name ),
    true,
    'has_trigger(schema, table, trigger)',
    'Table public.users should have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'users', 'set_users_pass', 'whatever' ),
    true,
    'has_trigger(table, trigger, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'users', 'set_users_pass' ),
    true,
    'has_trigger(table, trigger)',
    'Table users should have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'nosuch', 'whatever' ),
    false,
    'has_trigger(schema, table, nonexistent, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'users', 'nosuch' ),
    false,
    'has_trigger(table, nonexistent) no schema fail',
    'Table users should have trigger nosuch',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'public', 'users', 'set_users_pass', 'whatever' ),
    false,
    'hasnt_trigger(schema, table, trigger, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'public', 'users', 'set_users_pass'::name ),
    false,
    'hasnt_trigger(schema, table, trigger)',
    'Table public.users should not have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'users', 'set_users_pass', 'whatever' ),
    false,
    'hasnt_trigger(table, trigger, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'users', 'set_users_pass' ),
    false,
    'hasnt_trigger(table, trigger)',
    'Table users should not have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'public', 'users', 'nosuch', 'whatever' ),
    true,
    'hasnt_trigger(schema, table, nonexistent, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_trigger( 'users', 'nosuch' ),
    true,
    'hasnt_trigger(table, nonexistent) no schema fail',
    'Table users should not have trigger nosuch',
    ''
);

/****************************************************************************/
-- test trigger_is()

SELECT * FROM check_test(
    trigger_is( 'public', 'users', 'set_users_pass', 'public', 'hash_pass', 'whatever' ),
    true,
    'trigger_is()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    trigger_is( 'public', 'users', 'set_users_pass', 'public', 'hash_pass' ),
    true,
    'trigger_is() no desc',
    'Trigger set_users_pass should call public.hash_pass()',
    ''
);

SELECT * FROM check_test(
    trigger_is( 'users', 'set_users_pass', 'hash_pass', 'whatever' ),
    true,
    'trigger_is() no schema',
    'whatever',
    ''
);

SELECT * FROM check_test(
    trigger_is( 'users', 'set_users_pass', 'hash_pass' ),
    true,
    'trigger_is() no schema or desc',
    'Trigger set_users_pass should call hash_pass()',
    ''
);

SELECT * FROM check_test(
    trigger_is( 'public', 'users', 'set_users_pass', 'public', 'oops', 'whatever' ),
    false,
    'trigger_is() fail',
    'whatever',
    '        have: public.hash_pass
        want: public.oops'
);

SELECT * FROM check_test(
    trigger_is( 'users', 'set_users_pass', 'oops' ),
    false,
    'trigger_is() no schema fail',
    'Trigger set_users_pass should call oops()',
    '        have: hash_pass
        want: oops'
);

/****************************************************************************/
-- Test triggers_are().
SELECT * FROM check_test(
    triggers_are( 'public', 'users', ARRAY['set_users_pass', 'upd_users_pass'], 'whatever' ),
    true,
    'triggers_are(schema, table, triggers, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    triggers_are( 'public', 'users', ARRAY['set_users_pass', 'upd_users_pass'] ),
    true,
    'triggers_are(schema, table, triggers)',
    'Table public.users should have the correct triggers',
    ''
);

SELECT * FROM check_test(
    triggers_are( 'public', 'users', ARRAY['set_users_pass'] ),
    false,
    'triggers_are(schema, table, triggers) + extra',
    'Table public.users should have the correct triggers',
    '    Extra triggers:
        upd_users_pass'
);

SELECT * FROM check_test(
    triggers_are( 'public', 'users', ARRAY['set_users_pass', 'upd_users_pass', 'howdy'] ),
    false,
    'triggers_are(schema, table, triggers) + missing',
    'Table public.users should have the correct triggers',
    '    Missing triggers:
        howdy'
);

SELECT * FROM check_test(
    triggers_are( 'public', 'users', ARRAY['set_users_pass', 'howdy'] ),
    false,
    'triggers_are(schema, table, triggers) + extra & missing',
    'Table public.users should have the correct triggers',
    '    Extra triggers:
        upd_users_pass
    Missing triggers:
        howdy'
);

SELECT * FROM check_test(
    triggers_are( 'users', ARRAY['set_users_pass', 'upd_users_pass'], 'whatever' ),
    true,
    'triggers_are(table, triggers, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    triggers_are( 'users', ARRAY['set_users_pass', 'upd_users_pass'] ),
    true,
    'triggers_are(table, triggers)',
    'Table users should have the correct triggers',
    ''
);

SELECT * FROM check_test(
    triggers_are( 'users', ARRAY['set_users_pass'] ),
    false,
    'triggers_are(table, triggers) + extra',
    'Table users should have the correct triggers',
    '    Extra triggers:
        upd_users_pass'
);

SELECT * FROM check_test(
    triggers_are( 'users', ARRAY['set_users_pass', 'upd_users_pass', 'howdy'] ),
    false,
    'triggers_are(table, triggers) + missing',
    'Table users should have the correct triggers',
    '    Missing triggers:
        howdy'
);

SELECT * FROM check_test(
    triggers_are( 'users', ARRAY['set_users_pass', 'howdy'] ),
    false,
    'triggers_are(table, triggers) + extra & missing',
    'Table users should have the correct triggers',
    '    Extra triggers:
        upd_users_pass
    Missing triggers:
        howdy'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
