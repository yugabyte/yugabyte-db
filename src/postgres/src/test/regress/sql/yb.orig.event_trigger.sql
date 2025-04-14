
-- Test event trigger ordering.
-- If more than one event trigger is defined for a particular event,
-- they will fire in alphabetical order by trigger name.

-- Create the event trigger functions for logging.
-- Since event trigger functions cannot take arguments (and do not support
-- the tg_name variable) we have to create individual functions.
-- Make sure to create them in non-alphabetical order.
CREATE OR REPLACE FUNCTION test_event_trigger_ybc() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'Calling trigger ybc on %', tg_event;
END;
$$;

CREATE OR REPLACE FUNCTION test_event_trigger_xyz() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'Calling trigger xbc on %', tg_event;
END;
$$;

CREATE OR REPLACE FUNCTION test_event_trigger_abc() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'Calling trigger abc on %', tg_event;
END;
$$;

CREATE OR REPLACE FUNCTION test_event_trigger_foo() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'Calling trigger foo on %', tg_event;
END;
$$;

CREATE OR REPLACE FUNCTION test_event_trigger_bar() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'Calling trigger bar on %', tg_event;
END;
$$;

-- Create the ddl_start event triggers.
-- Make sure to create them in non-alphabetical order.
-- (and different from functions order).
CREATE EVENT TRIGGER foo ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_foo();
CREATE EVENT TRIGGER ybc ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_ybc();
CREATE EVENT TRIGGER bar ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_bar();
CREATE EVENT TRIGGER xyz ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_xyz();
CREATE EVENT TRIGGER abc ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_abc();

-- Create the ddl_end event triggers.
-- Make sure to create them in non-alphabetical order
-- (and different from functions or start triggers order).
CREATE EVENT TRIGGER end_xyz ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_xyz();
CREATE EVENT TRIGGER end_bar ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_bar();
CREATE EVENT TRIGGER end_ybc ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_ybc();
CREATE EVENT TRIGGER end_abc ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_abc();
CREATE EVENT TRIGGER end_foo ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_foo();

CREATE TABLE test(a int PRIMARY KEY, b int);

INSERT INTO test values (1,2);
INSERT INTO test values (2,3);
INSERT INTO test values (3,4);
SELECT * FROM test;

DROP TABLE test;

-- Verify that yb_db_admin role can use event triggers.
CREATE ROLE superuser_role SUPERUSER;
CREATE ROLE non_superuser_role;
SET SESSION AUTHORIZATION yb_db_admin;
CREATE EVENT TRIGGER admin_foo ON ddl_command_start EXECUTE PROCEDURE test_event_trigger_foo();
CREATE EVENT TRIGGER admin_bar ON ddl_command_end EXECUTE PROCEDURE test_event_trigger_bar();
ALTER EVENT TRIGGER admin_foo DISABLE;
ALTER EVENT TRIGGER admin_foo ENABLE REPLICA;
ALTER EVENT TRIGGER admin_foo ENABLE ALWAYS;
ALTER EVENT TRIGGER admin_foo RENAME TO admin_foo_new;
ALTER EVENT TRIGGER admin_foo_new OWNER TO superuser_role;
ALTER EVENT TRIGGER admin_foo_new OWNER TO non_superuser_role;
ALTER EVENT TRIGGER admin_foo_new OWNER TO yb_db_admin;
DROP EVENT TRIGGER admin_foo_new;
