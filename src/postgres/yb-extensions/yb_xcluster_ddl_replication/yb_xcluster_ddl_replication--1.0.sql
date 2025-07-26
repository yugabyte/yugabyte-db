-- Complain if script is sourced in psql, rather than via CREATE EXTENSION.
\echo Use "CREATE EXTENSION yb_xcluster_ddl_replication" to load this file \quit

/* ------------------------------------------------------------------------- */
/* Create extension tables. */

CREATE TABLE yb_xcluster_ddl_replication.ddl_queue(
  ddl_end_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (ddl_end_time, query_id)) WITH (COLOCATION = false);

CREATE TABLE yb_xcluster_ddl_replication.replicated_ddls(
  ddl_end_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (ddl_end_time, query_id)) WITH (COLOCATION = false);

/* ------------------------------------------------------------------------- */
/* Create routines for user of extension. */

CREATE FUNCTION yb_xcluster_ddl_replication.get_replication_role()
  RETURNS text
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'get_replication_role';

/* ------------------------------------------------------------------------- */
/* Create event triggers. */

CREATE FUNCTION yb_xcluster_ddl_replication.handle_ddl_start()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_ddl_start';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_ddl_start_trigger
  ON ddl_command_start
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_start();

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_truncate_start_trigger
  ON ddl_command_start
  WHEN TAG in ('TRUNCATE TABLE')
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_start();

CREATE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_ddl_end';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_ddl_end_trigger
  ON ddl_command_end
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end();

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_truncate_end_trigger
  ON ddl_command_end
  WHEN TAG in ('TRUNCATE TABLE')
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end();

CREATE FUNCTION yb_xcluster_ddl_replication.handle_sql_drop()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_sql_drop';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_sql_drop_trigger
  ON sql_drop
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_sql_drop();

CREATE FUNCTION yb_xcluster_ddl_replication.handle_table_rewrite()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_table_rewrite';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_table_rewrite_trigger
  ON table_rewrite
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_table_rewrite();

/* ------------------------------------------------------------------------- */
/* Set allowed access. */

GRANT USAGE ON SCHEMA yb_xcluster_ddl_replication TO PUBLIC;
-- At this point access for non-super users is allowed only to functions and procedures.

REVOKE EXECUTE ON ALL FUNCTIONS IN SCHEMA yb_xcluster_ddl_replication FROM PUBLIC;
REVOKE EXECUTE ON ALL PROCEDURES IN SCHEMA yb_xcluster_ddl_replication FROM PUBLIC;

GRANT EXECUTE ON FUNCTION yb_xcluster_ddl_replication.get_replication_role() TO PUBLIC;
