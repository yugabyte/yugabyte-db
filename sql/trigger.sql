\unset ECHO
\i test_setup.sql

SELECT plan(33);
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
BEFORE INSERT OR UPDATE ON public.users
FOR EACH ROW EXECUTE PROCEDURE hash_pass();
RESET client_min_messages;

/****************************************************************************/
-- Test has_trigger().

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'set_users_pass', 'whatever' ),
    true,
    'has_trigger()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'set_users_pass' ),
    true,
    'has_trigger() no desc',
    'Table public.users should have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'users', 'set_users_pass' ),
    true,
    'has_trigger() no schema',
    'Table users should have trigger set_users_pass',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'public', 'users', 'nosuch', 'whatever' ),
    false,
    'has_trigger() fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_trigger( 'users', 'nosuch' ),
    false,
    'has_trigger() no schema fail',
    'Table users should have trigger nosuch',
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
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
