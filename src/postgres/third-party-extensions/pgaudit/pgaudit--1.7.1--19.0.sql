-- complain if script is sourced in psql, rather than via ALTER EXTENSION
\echo Use "ALTER EXTENSION pgaudit UPDATE" to load this file.\quit

ALTER FUNCTION pgaudit_ddl_command_end()
	SET search_path = pg_catalog, pg_temp;

ALTER FUNCTION pgaudit_sql_drop()
	SET search_path = pg_catalog, pg_temp;
