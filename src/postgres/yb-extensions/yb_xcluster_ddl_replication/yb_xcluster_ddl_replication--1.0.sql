-- Complain if script is sourced in psql, rather than via CREATE EXTENSION.
\echo Use "CREATE EXTENSION yb_xcluster_ddl_replication" to load this file \quit

/* ------------------------------------------------------------------------- */
/* Create extension tables. */

CREATE TABLE yb_xcluster_ddl_replication.ddl_queue(
  start_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (start_time, query_id))
WITH (COLOCATION = false)
SPLIT INTO 1 TABLETS;

CREATE TABLE yb_xcluster_ddl_replication.replicated_ddls(
  start_time bigint NOT NULL,
  query_id bigint NOT NULL,
  yb_data jsonb NOT NULL,
  PRIMARY KEY (start_time, query_id))
WITH (COLOCATION = false)
SPLIT INTO 1 TABLETS;

/* ------------------------------------------------------------------------- */
/* Create event triggers. */

CREATE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end()
  RETURNS event_trigger
  LANGUAGE C
  AS 'MODULE_PATHNAME', 'handle_ddl_end';

CREATE EVENT TRIGGER yb_xcluster_ddl_replication_handle_ddl_end
  ON ddl_command_end
  EXECUTE FUNCTION yb_xcluster_ddl_replication.handle_ddl_end();
