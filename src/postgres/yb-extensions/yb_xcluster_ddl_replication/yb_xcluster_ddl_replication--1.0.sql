-- Complain if script is sourced in psql, rather than via CREATE EXTENSION.
\echo Use "CREATE EXTENSION yb_xcluster_ddl_replication" to load this file \quit

/* ------------------------------------------------------------------------- */
/* Create extension tables. */

CREATE TABLE yb_xcluster_ddl_replication.ddl_queue(
  start_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (start_time, query_id));

CREATE TABLE yb_xcluster_ddl_replication.replicated_ddls(
  start_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (start_time, query_id));

/* ------------------------------------------------------------------------- */
/* Create event triggers. */

CREATE FUNCTION yb_xcluster_ddl_replication.handle_ddl_start()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_ddl_start';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_ddl_start_trigger
  ON ddl_command_start
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_start();

CREATE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_ddl_end';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_ddl_end_trigger
  ON ddl_command_end
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end();

CREATE FUNCTION yb_xcluster_ddl_replication.handle_sql_drop()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_sql_drop';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_sql_drop_trigger
  ON sql_drop
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_sql_drop();
