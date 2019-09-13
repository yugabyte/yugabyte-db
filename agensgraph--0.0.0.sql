-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION agensgraph" to load this file. \quit

--
-- catalog tables
--

CREATE TABLE ag_graph(
  name name NOT NULL,
  namespace regnamespace NOT NULL
);
CREATE UNIQUE INDEX ag_graph_name_index ON ag_graph USING btree (name);

--
-- utility functions
--

CREATE FUNCTION create_graph(graph_name name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION drop_graph(graph_name name, cascade bool = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION alter_graph(graph_name name, operation cstring, new_value name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

--
-- query functions
--

CREATE FUNCTION cypher(query_string cstring)
RETURNS SETOF record
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION get_cypher_keywords(OUT word text, OUT catcode "char",
                                    OUT catdesc text)
RETURNS SETOF record
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
COST 10
ROWS 60
AS 'MODULE_PATHNAME';

--
-- jsonbx type and support functions
--

CREATE TYPE jsonbx;

CREATE FUNCTION jsonbx_in(cstring)
RETURNS jsonbx
LANGUAGE C
STABLE
STRICT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION jsonbx_out(jsonbx)
RETURNS cstring
LANGUAGE C
STABLE
STRICT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE jsonbx (
INPUT = jsonbx_in,
OUTPUT = jsonbx_out,
LIKE = jsonb,
CATEGORY = 'U',
PREFERRED = FALSE,
DELIMITER = ',',
COLLATABLE = FALSE
);
