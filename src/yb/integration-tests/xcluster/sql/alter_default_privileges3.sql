-- A statement that returns a role's default privileges to the built-in
-- defaults deletes its pg_default_acl row, which DDL replication sees as a
-- dropped default ACL object.

-- These REVOKEs undo alter_default_privileges2.sql's first two GRANTs,
-- deleting their rows.
ALTER DEFAULT PRIVILEGES REVOKE SELECT ON TABLES FROM sandeep;
ALTER DEFAULT PRIVILEGES IN SCHEMA public REVOKE SELECT ON TABLES FROM sandeep;

-- alter_default_privileges.sql's REVOKE took SELECT away from sandeep's own
-- built-in defaults, creating a row owned by sandeep.  Granting SELECT back
-- returns sandeep to the built-in defaults, deleting that row.
SET ROLE sandeep;
ALTER DEFAULT PRIVILEGES GRANT SELECT ON TABLES TO sandeep;
RESET ROLE;

-- A schema-specific row depends on its schema, so dropping the schema also
-- deletes the row.
DROP SCHEMA s;
