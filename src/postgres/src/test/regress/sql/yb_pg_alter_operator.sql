
--
-- ALTER OPERATOR
--
-- Based on "alter_operator" test.
--

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

SELECT pg_describe_object(refclassid,refobjid,refobjsubid) as ref, deptype
FROM pg_depend
WHERE classid = 'pg_operator'::regclass AND
      objid = '===(bool,bool)'::regoperator
ORDER BY 1;

--
-- Reset and set params
--

ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = NONE);
ALTER OPERATOR === (boolean, boolean) SET (JOIN = NONE);

SELECT oprrest, oprjoin FROM pg_operator WHERE oprname = '==='
  AND oprleft = 'boolean'::regtype AND oprright = 'boolean'::regtype;

SELECT pg_describe_object(refclassid,refobjid,refobjsubid) as ref, deptype
FROM pg_depend
WHERE classid = 'pg_operator'::regclass AND
      objid = '===(bool,bool)'::regoperator
ORDER BY 1;

ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = contsel);
ALTER OPERATOR === (boolean, boolean) SET (JOIN = contjoinsel);

SELECT oprrest, oprjoin FROM pg_operator WHERE oprname = '==='
  AND oprleft = 'boolean'::regtype AND oprright = 'boolean'::regtype;

SELECT pg_describe_object(refclassid,refobjid,refobjsubid) as ref, deptype
FROM pg_depend
WHERE classid = 'pg_operator'::regclass AND
      objid = '===(bool,bool)'::regoperator
ORDER BY 1;

ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = NONE, JOIN = NONE);

SELECT oprrest, oprjoin FROM pg_operator WHERE oprname = '==='
  AND oprleft = 'boolean'::regtype AND oprright = 'boolean'::regtype;

SELECT pg_describe_object(refclassid,refobjid,refobjsubid) as ref, deptype
FROM pg_depend
WHERE classid = 'pg_operator'::regclass AND
      objid = '===(bool,bool)'::regoperator
ORDER BY 1;

ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = customcontsel, JOIN = contjoinsel);

SELECT oprrest, oprjoin FROM pg_operator WHERE oprname = '==='
  AND oprleft = 'boolean'::regtype AND oprright = 'boolean'::regtype;

SELECT pg_describe_object(refclassid,refobjid,refobjsubid) as ref, deptype
FROM pg_depend
WHERE classid = 'pg_operator'::regclass AND
      objid = '===(bool,bool)'::regoperator
ORDER BY 1;

--
-- Test invalid options.
--
ALTER OPERATOR === (boolean, boolean) SET (COMMUTATOR = ====);
ALTER OPERATOR === (boolean, boolean) SET (NEGATOR = ====);
ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = non_existent_func);
ALTER OPERATOR === (boolean, boolean) SET (JOIN = non_existent_func);
ALTER OPERATOR === (boolean, boolean) SET (COMMUTATOR = !==);
ALTER OPERATOR === (boolean, boolean) SET (NEGATOR = !==);

-- invalid: non-lowercase quoted identifiers
ALTER OPERATOR & (bit, bit) SET ("Restrict" = _int_contsel, "Join" = _int_contjoinsel);

--
-- Test permission check. Must be owner to ALTER OPERATOR.
--
CREATE USER regress_alter_op_user;
SET SESSION AUTHORIZATION regress_alter_op_user;

ALTER OPERATOR === (boolean, boolean) SET (RESTRICT = NONE); -- error

-- SET OWNER check
RESET SESSION AUTHORIZATION;
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

--
-- Cleanup
--
DROP OPERATOR === (boolean, boolean);
DROP FUNCTION customcontsel(internal, oid, internal, integer);
DROP FUNCTION alter_op_test_fn(boolean, boolean);
