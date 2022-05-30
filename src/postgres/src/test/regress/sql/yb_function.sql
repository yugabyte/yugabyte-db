CREATE USER regress_alter_generic_user3;
CREATE USER regress_alter_generic_user2;
CREATE USER regress_alter_generic_user1 IN ROLE regress_alter_generic_user3;
CREATE SCHEMA alt_nsp1;
GRANT ALL ON SCHEMA alt_nsp1 TO PUBLIC;
SET search_path = alt_nsp1, public;

---
--- Verify yb_db_admin can CREATE and DROP functions
---
CREATE FUNCTION other_func(int) RETURNS int LANGUAGE sql
  AS 'SELECT $1 + 1';
SET SESSION AUTHORIZATION yb_db_admin;
CREATE FUNCTION admin_func(int) RETURNS int LANGUAGE sql
  AS 'SELECT $1 + 1';
CREATE FUNCTION admin_func_leakproof(int) RETURNS int LANGUAGE sql  -- not allowed
  LEAKPROOF AS 'SELECT $1 + 1';
CREATE OR REPLACE FUNCTION increment(i integer) RETURNS integer AS $$
  BEGIN
    RETURN i + 1;
  END;
$$ LANGUAGE plpgsql;
CREATE FUNCTION language_func() RETURNS uuid  -- C functions aren't allowed
  LANGUAGE c STRICT PARALLEL SAFE
  AS '$libdir/uuid-ossp', 'uuid_generate_v1';
DROP FUNCTION admin_func(int);
DROP FUNCTION other_func(int);
DROP FUNCTION language_func(); -- does not exist
RESET SESSION AUTHORIZATION;

---
--- Validate yb_db_admin role can ALTER function
---
CREATE FUNCTION alt_func1(int) RETURNS int LANGUAGE sql
  AS 'SELECT $1 + 1';
SET SESSION AUTHORIZATION yb_db_admin;
ALTER FUNCTION alt_func1(int) OWNER TO regress_alter_generic_user1;
ALTER FUNCTION alt_func1(int) RENAME TO func_renamed;
ALTER FUNCTION func_renamed(int) SET SCHEMA alt_nsp1;
ALTER FUNCTION func_renamed(int) LEAKPROOF;  -- not allowed
ALTER FUNCTION func_renamed(int) NOT LEAKPROOF;
-- validate regress_alter_generic_user2 can operate on function
SET SESSION AUTHORIZATION regress_alter_generic_user1;
ALTER FUNCTION func_renamed(int) OWNER TO regress_alter_generic_user2;  -- failed (no role membership)
ALTER FUNCTION func_renamed(int) OWNER TO regress_alter_generic_user3;  -- OK
ALTER FUNCTION func_renamed(int) RENAME TO func_renamed2;
ALTER FUNCTION func_renamed2(int) SET SCHEMA alt_nsp1;
ALTER FUNCTION func_renamed2(int) LEAKPROOF;  -- not allowed
ALTER FUNCTION func_renamed2(int) NOT LEAKPROOF;

---
--- Clean up
---
RESET SESSION AUTHORIZATION;
DROP SCHEMA alt_nsp1 CASCADE;
DROP USER regress_alter_generic_user1;
DROP USER regress_alter_generic_user2;
DROP USER regress_alter_generic_user3;
