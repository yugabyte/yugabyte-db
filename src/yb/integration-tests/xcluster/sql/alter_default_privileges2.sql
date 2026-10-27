-- A pg_default_acl row exists only while a role's default privileges differ
-- from the built-in defaults.  This file creates rows that the following files
-- delete.

ALTER DEFAULT PRIVILEGES GRANT SELECT ON TABLES TO sandeep;

-- Same as above, but using the schema-specific form.  A schema-specific row's
-- built-in default is no privileges at all.
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO sandeep;

CREATE SCHEMA s;
ALTER DEFAULT PRIVILEGES IN SCHEMA s GRANT SELECT ON TABLES TO PUBLIC;

-- Rows for alter_default_privileges4.sql's DROP OWNED: one owned by sandeep
-- and one granting to sandeep.  They are for sequences, not tables, so that
-- alter_default_privileges3.sql does not touch them.
SET ROLE sandeep;
ALTER DEFAULT PRIVILEGES REVOKE SELECT ON SEQUENCES FROM sandeep;
RESET ROLE;
ALTER DEFAULT PRIVILEGES GRANT SELECT ON SEQUENCES TO sandeep;
