CREATE FUNCTION alter_op_test_fn(boolean, boolean)
RETURNS boolean AS $$ SELECT NULL::BOOLEAN; $$ LANGUAGE sql IMMUTABLE;

CREATE FUNCTION customcontsel(internal, oid, internal, integer)
RETURNS float8 AS 'contsel' LANGUAGE internal STABLE STRICT;

CREATE OPERATOR === (
    LEFTARG = boolean,
    RIGHTARG = boolean,
    PROCEDURE = alter_op_test_fn,
    COMMUTATOR = ===,
    NEGATOR = !==,
    RESTRICT = customcontsel,
    JOIN = contjoinsel,
    HASHES, MERGES
);

CREATE USER regress_alter_op_user;
ALTER OPERATOR === (boolean, boolean) OWNER TO regress_alter_op_user;
DROP USER regress_alter_op_user; -- error
SET SESSION AUTHORIZATION regress_alter_op_user;
ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = NONE);
SELECT oprrest FROM pg_operator WHERE oprname = '==='
  AND oprleft = 'boolean'::regtype AND oprright = 'boolean'::regtype;

-- Cleanup
RESET SESSION AUTHORIZATION;
ALTER OPERATOR === (boolean, boolean) OWNER TO yugabyte;
DROP USER regress_alter_op_user;

--
-- Test SET SCHEMA
--
CREATE SCHEMA op_schema;
ALTER OPERATOR === (boolean, boolean) SET SCHEMA op_schema;
SELECT ns.nspname FROM pg_operator op
    INNER JOIN pg_namespace ns ON ns.oid = op.oprnamespace
    WHERE op.oprname = '==='
      AND op.oprleft = 'boolean'::regtype AND op.oprright = 'boolean'::regtype;

-- Cleanup
ALTER OPERATOR op_schema.=== (boolean, boolean) SET SCHEMA public;
DROP SCHEMA op_schema;
