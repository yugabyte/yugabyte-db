#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh
source "${BASH_SOURCE[0]%/*}"/common_upgrade.sh

run_and_pushd_pg11
popd

# Run preflight checks. The check should pass by default.
run_preflight_checks

# Disallow connections on the 'postgres' database
ysqlsh <<EOT
ALTER DATABASE postgres WITH allow_connections FALSE;
EOT

# Check failure
grep -q "All non-template0 databases must allow connections" \
  <(run_preflight_checks 2>&1)

# Re-enable connections on the 'postgres' database
ysqlsh <<EOT
ALTER DATABASE postgres WITH allow_connections TRUE;
EOT

# Create table using pg_authid (a system-defined composite type)
ysqlsh <<EOT
CREATE TABLE system_composite_test (id int primary key, authid pg_authid);
INSERT INTO system_composite_test VALUES (1, null);
EOT

# Check failure
grep -q "Your installation contains system-defined composite type(s) in user tables." \
  <(run_preflight_checks 2>&1)

# Drop table using pg_authid
ysqlsh <<EOT
DROP TABLE system_composite_test;
EOT

# Use regproc in a table
ysqlsh <<EOT
CREATE TABLE reg_check (id int primary key, reg_field regproc);
INSERT INTO reg_check VALUES (1, 1);
SELECT * from reg_check;
EOT

# Check failure
grep -q "Your installation contains one of the reg\*" \
  <(run_preflight_checks 2>&1)

# Drop table with regproc
ysqlsh <<EOT
DROP TABLE reg_check;
EOT

# Create a postfix operator
ysqlsh << 'EOT'
CREATE FUNCTION ident(integer)
RETURNS integer
AS $$
BEGIN
    RETURN $1;
END;
$$ LANGUAGE plpgsql;

CREATE OPERATOR !!! (
    LEFTARG = integer,
    PROCEDURE = ident
);
EOT

# Check failure
grep -q "Your installation contains user-defined postfix operators" \
  <(run_preflight_checks 2>&1)

# Drop the postfix operator
ysqlsh <<EOT
DROP FUNCTION ident(integer) CASCADE;
EOT

# Use anyarray/anyelement in polymorphic function
ysqlsh <<EOT
CREATE AGGREGATE array_accum (ANYELEMENT)
(
    sfunc = array_append,
    stype = ANYARRAY,
    initcond = '{}'
);

CREATE TABLE polymorph_table (
    id SERIAL PRIMARY KEY,
    data TEXT
);

INSERT INTO polymorph_table (data) VALUES
    ('apple'), ('banana'), ('cherry'), ('orange');

SELECT array_accum(data) AS data_array FROM polymorph_table;
EOT

# Check failure
grep -qE "Checking for incompatible polymorphic functions\s+fatal" \
  <(run_preflight_checks 2>&1)

# Drop polymorph_table
ysqlsh <<EOT
DROP TABLE polymorph_table;
DROP AGGREGATE array_accum (ANYELEMENT);
EOT

# Create table using sql_identifier
ysqlsh <<EOT
CREATE TABLE sql_identifier_test (id int primary key, d information_schema.sql_identifier);
INSERT INTO sql_identifier_test VALUES (1, 'test');
EOT

# Check failure
grep -q "Your installation contains the \"sql_identifier\"" \
  <(run_preflight_checks 2>&1)

# Drop table using sql_identifier
ysqlsh <<EOT
DROP TABLE sql_identifier_test;
EOT

# Drop postgres database
ysqlsh <<EOT
DROP DATABASE postgres;
EOT

# Check failure
grep -q "The source cluster does not have the database: \"postgres\"" \
  <(run_preflight_checks 2>&1)

# Create postgres database
ysqlsh <<EOT
CREATE DATABASE postgres;
EOT

# Run preflight checks again. The final check should pass.
run_preflight_checks
