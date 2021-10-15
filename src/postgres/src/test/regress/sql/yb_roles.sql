-- test YB roles
CREATE USER regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto; -- should fail
\c -
GRANT yb_extension TO regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto;
ALTER EXTENSION pgcrypto UPDATE TO '1.3';
DROP EXTENSION pgcrypto;
\c -
DROP USER regress_priv_user;
