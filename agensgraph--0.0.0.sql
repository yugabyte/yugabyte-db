-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION agensgraph" to load this file. \quit

CREATE TABLE ag_graph(
  name name NOT NULL,
  namespace regnamespace NOT NULL
);
CREATE UNIQUE INDEX ag_graph_name_index ON ag_graph USING btree (name);

CREATE FUNCTION create_graph(graph_name name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION drop_graph(graph_name name, cascade bool = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION cypher(query_string cstring)
RETURNS SETOF record
LANGUAGE c
AS 'MODULE_PATHNAME';
