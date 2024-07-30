\unset ECHO
\i test/psql.sql

/*
 * Presumably no one is installing this way anymore, but this is a nice way to
 * pick up any syntax errors during install.
 */
BEGIN;
\i sql/pgtap.sql
ROLLBACK;
