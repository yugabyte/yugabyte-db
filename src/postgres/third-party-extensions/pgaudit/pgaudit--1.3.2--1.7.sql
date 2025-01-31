-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgaudit" to load this file.\quit

-- Drop old triggers and functions
drop event trigger if exists pgaudit_ddl_command_end;
drop function if exists pgaudit_ddl_command_end();

drop event trigger if exists pgaudit_sql_drop;
drop function if exists pgaudit_sql_drop();

-- Create triggers and functions with security definer and search_path
CREATE FUNCTION pgaudit_ddl_command_end()
	RETURNS event_trigger
	SECURITY DEFINER
	SET search_path = 'pg_catalog, pg_temp'
	LANGUAGE C
	AS 'MODULE_PATHNAME', 'pgaudit_ddl_command_end';

CREATE EVENT TRIGGER pgaudit_ddl_command_end
	ON ddl_command_end
	EXECUTE PROCEDURE pgaudit_ddl_command_end();

CREATE FUNCTION pgaudit_sql_drop()
	RETURNS event_trigger
	SECURITY DEFINER
	SET search_path = 'pg_catalog, pg_temp'
	LANGUAGE C
	AS 'MODULE_PATHNAME', 'pgaudit_sql_drop';

CREATE EVENT TRIGGER pgaudit_sql_drop
	ON sql_drop
	EXECUTE PROCEDURE pgaudit_sql_drop();
