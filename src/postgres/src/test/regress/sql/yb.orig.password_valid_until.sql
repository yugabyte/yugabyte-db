--
-- yb_password_validity Testsuite: duration-based password expiration.
--

-- Without a policy configured, VALID UNTIL behaves exactly as in PostgreSQL.
SHOW yb_password_validity;
CREATE ROLE regress_pw_nopolicy PASSWORD 'secret';
SELECT rolvaliduntil IS NULL AS unset
  FROM pg_authid WHERE rolname = 'regress_pw_nopolicy';

--
-- Vaildate that yb_password_validity can be set only in specific circumstances. per role,
--

-- A plain SET/RESET is rejected outright, even for a superuser.
SET yb_password_validity = 0; -- error: session-scoped
RESET yb_password_validity;   -- no-op: RESET re-applies the existing default
                              -- without going through this check

-- ALTER SYSTEM SET is a valid way to set password validity.
-- However, YB does not support this command yet.
ALTER SYSTEM SET yb_password_validity = '15d';
ALTER SYSTEM RESET yb_password_validity;

-- ALTER DATABASE ... SET is rejected: roles aren't database-scoped.
CREATE DATABASE regress_pw_testdb;
ALTER DATABASE regress_pw_testdb SET yb_password_validity = '10d';
DROP DATABASE regress_pw_testdb;

-- ALTER ROLE ... IN DATABASE ... SET is rejected for the same reason.
SELECT current_database() AS cur_db \gset
CREATE ROLE regress_pw_scope_role LOGIN PASSWORD 'secret';
ALTER ROLE regress_pw_scope_role IN DATABASE :cur_db SET yb_password_validity = '10d';

-- ALTER ROLE ... SET is rejected for a role that cannot log in
CREATE ROLE regress_pw_nologin NOLOGIN;
ALTER ROLE regress_pw_nologin SET yb_password_validity = '10d';
DROP ROLE regress_pw_nologin;

-- ALTER ROLE ... SET is accepted for a role that can log in. It applies the
-- next time that specific role connects -- not to the current session, and
-- not to any other role.
SELECT current_user AS cur_user \gset
ALTER ROLE :cur_user SET yb_password_validity = '20d';
ALTER ROLE regress_pw_scope_role SET yb_password_validity = '25d';
SHOW yb_password_validity;

\c - regress_pw_scope_role
SHOW yb_password_validity;

\c - :cur_user
SHOW yb_password_validity;

ALTER ROLE :cur_user RESET yb_password_validity;
ALTER ROLE regress_pw_scope_role RESET yb_password_validity;

-- yb_db_admin cannot set the GUC directly either; it is restricted the same
-- way as any other role for now (extending its access is future work).
SET SESSION AUTHORIZATION yb_db_admin;
SET yb_password_validity = '30d';
RESET SESSION AUTHORIZATION;

-- ALTER ROLE ALL SET establishes a default for every role's future
-- sessions, including this one; reconnect to pick it up.
ALTER ROLE ALL SET yb_password_validity = '60d';
\c - regress_pw_scope_role
SHOW yb_password_validity;

\c - :cur_user
SHOW yb_password_validity;

-- ALTER ROLE <user> SET ... takes precedence over ALTER ROLE ALL SET ...
ALTER ROLE regress_pw_scope_role SET yb_password_validity = '70d';
ALTER ROLE ALL SET yb_password_validity = '80d';

\c - regress_pw_scope_role
SHOW yb_password_validity;

-- And when ALTER ROLE <user> SET ... is reset, password validity automatically
-- falls back to ALTER ROLE ALL SET ...
\c - :cur_user
ALTER ROLE regress_pw_scope_role RESET yb_password_validity;

\c - regress_pw_scope_role
SHOW yb_password_validity;

\c - :cur_user
DROP ROLE regress_pw_scope_role;

-- CREATE ROLE
CREATE ROLE regress_pw_create PASSWORD 'secret';
SELECT rolvaliduntil BETWEEN now() + interval '79 days'
                         AND now() + interval '81 days' AS expires_in_80_days
  FROM pg_authid WHERE rolname = 'regress_pw_create';

-- No password, so there is nothing to expire.
CREATE ROLE regress_pw_nopass;
SELECT rolvaliduntil IS NULL AS unset
  FROM pg_authid WHERE rolname = 'regress_pw_nopass';

-- An explicit VALID UNTIL wins over the policy.
CREATE ROLE regress_pw_explicit PASSWORD 'secret' VALID UNTIL 'infinity';
SELECT rolvaliduntil FROM pg_authid WHERE rolname = 'regress_pw_explicit';

-- An empty password is cleared, so again there is nothing to expire.
CREATE ROLE regress_pw_empty PASSWORD '';
SELECT rolpassword IS NULL AS no_password, rolvaliduntil IS NULL AS unset
  FROM pg_authid WHERE rolname = 'regress_pw_empty';

-- ALTER ROLE
ALTER ROLE ALL SET yb_password_validity = '90d';
\c -

-- Every password change re-stamps the expiration.
ALTER ROLE regress_pw_create PASSWORD 'secret2';
SELECT rolvaliduntil BETWEEN now() + interval '89 days'
                         AND now() + interval '91 days' AS expires_in_90_days
  FROM pg_authid WHERE rolname = 'regress_pw_create';

-- Including for a role whose expiration was previously set by hand.
ALTER ROLE regress_pw_explicit PASSWORD 'secret2';
SELECT rolvaliduntil BETWEEN now() + interval '89 days'
                         AND now() + interval '91 days' AS expires_in_90_days
  FROM pg_authid WHERE rolname = 'regress_pw_explicit';

-- An administrator can still override the expiration, with or without setting
-- a password in the same statement.
ALTER ROLE regress_pw_explicit VALID UNTIL 'infinity';
SELECT rolvaliduntil FROM pg_authid WHERE rolname = 'regress_pw_explicit';
ALTER ROLE regress_pw_explicit PASSWORD 'secret3' VALID UNTIL 'infinity';
SELECT rolvaliduntil FROM pg_authid WHERE rolname = 'regress_pw_explicit';

-- Removing the password leaves the expiration alone.
ALTER ROLE regress_pw_explicit PASSWORD NULL;
SELECT rolpassword IS NULL AS no_password, rolvaliduntil
  FROM pg_authid WHERE rolname = 'regress_pw_explicit';
ALTER ROLE regress_pw_create PASSWORD '';
SELECT rolpassword IS NULL AS no_password,
       rolvaliduntil BETWEEN now() + interval '89 days'
                         AND now() + interval '91 days' AS unchanged
  FROM pg_authid WHERE rolname = 'regress_pw_create';

-- A role changing its own password is subject to the policy.
ALTER ROLE ALL SET yb_password_validity = '30d';
\c -
CREATE ROLE regress_pw_self LOGIN PASSWORD 'secret';
SET SESSION AUTHORIZATION regress_pw_self;
SET yb_password_validity = 0; -- permission denied
ALTER DATABASE template1 SET yb_password_validity = '10d'; -- permission denied
ALTER ROLE regress_pw_self PASSWORD 'secret2';
RESET SESSION AUTHORIZATION;
SELECT rolvaliduntil BETWEEN now() + interval '29 days'
                         AND now() + interval '31 days' AS expires_in_30_days
  FROM pg_authid WHERE rolname = 'regress_pw_self';

-- A later global default does not replace the current setting.
ALTER ROLE ALL SET yb_password_validity = '90d';
SHOW yb_password_validity;
ALTER ROLE ALL RESET yb_password_validity;

-- A per-role default overrides the (now-unset) global default for that
-- role's own sessions, and requires a genuine reconnect to take effect --
-- SET SESSION AUTHORIZATION does not re-run this lookup.
ALTER ROLE regress_pw_self SET yb_password_validity = '10d';
\c - regress_pw_self
SHOW yb_password_validity;
ALTER ROLE regress_pw_self PASSWORD 'secret3';
\c - :cur_user
SELECT rolvaliduntil BETWEEN now() + interval '9 days'
                         AND now() + interval '11 days' AS expires_in_10_days
  FROM pg_authid WHERE rolname = 'regress_pw_self';

-- Bounds of the GUC.
ALTER ROLE ALL SET yb_password_validity = -1; -- fail

-- When the GUC is 0, VALID UNTIL is left alone (no restamp on password change).
SHOW yb_password_validity;
ALTER ROLE regress_pw_explicit PASSWORD 'secret4';
SELECT rolvaliduntil FROM pg_authid WHERE rolname = 'regress_pw_explicit';
CREATE ROLE regress_pw_zero PASSWORD 'secret' VALID UNTIL 'infinity';
SELECT rolvaliduntil FROM pg_authid WHERE rolname = 'regress_pw_zero';

DROP ROLE regress_pw_nopolicy, regress_pw_create, regress_pw_nopass,
          regress_pw_explicit, regress_pw_empty, regress_pw_self,
          regress_pw_zero;
