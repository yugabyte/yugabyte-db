/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION age" to load this file. \quit

--
-- catalog tables
--

CREATE TABLE ag_graph (
  name name NOT NULL,
  namespace regnamespace NOT NULL
) WITH (OIDS);

CREATE UNIQUE INDEX ag_graph_oid_index ON ag_graph USING btree (oid);

CREATE UNIQUE INDEX ag_graph_name_index ON ag_graph USING btree (name);

CREATE UNIQUE INDEX ag_graph_namespace_index
ON ag_graph
USING btree (namespace);

-- 0 is an invalid label ID
CREATE DOMAIN label_id AS int NOT NULL CHECK (VALUE > 0 AND VALUE <= 65535);

CREATE DOMAIN label_kind AS "char" NOT NULL CHECK (VALUE = 'v' OR VALUE = 'e');

CREATE TABLE ag_label (
  name name NOT NULL,
  graph oid NOT NULL,
  id label_id,
  kind label_kind,
  relation regclass NOT NULL
) WITH (OIDS);

CREATE UNIQUE INDEX ag_label_oid_index ON ag_label USING btree (oid);

CREATE UNIQUE INDEX ag_label_name_graph_index
ON ag_label
USING btree (name, graph);

CREATE UNIQUE INDEX ag_label_graph_id_index
ON ag_label
USING btree (graph, id);

CREATE UNIQUE INDEX ag_label_relation_index ON ag_label USING btree (relation);

--
-- catalog lookup functions
--

CREATE FUNCTION _label_id(graph_name name, label_name name)
RETURNS label_id
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- utility functions
--

CREATE FUNCTION create_graph(graph_name name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION drop_graph(graph_name name, cascade boolean = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION alter_graph(graph_name name, operation cstring, new_value name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION drop_label(graph_name name, label_name name,
                           force boolean = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

--
-- graphid type
--

-- define graphid as a shell type first
CREATE TYPE graphid;

CREATE FUNCTION graphid_in(cstring)
RETURNS graphid
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION graphid_out(graphid)
RETURNS cstring
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE graphid (
  INPUT = graphid_in,
  OUTPUT = graphid_out,
  INTERNALLENGTH = 8,
  PASSEDBYVALUE,
  ALIGNMENT = float8,
  STORAGE = plain
);

--
-- graphid - comparison operators (=, <>, <, >, <=, >=)
--

CREATE FUNCTION graphid_eq(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = graphid_eq,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION graphid_ne(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = graphid_ne,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION graphid_lt(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = graphid_lt,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION graphid_gt(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = graphid_gt,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION graphid_le(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = graphid_le,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION graphid_ge(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = graphid_ge,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

--
-- graphid - B-tree support functions
--

-- comparison support
CREATE FUNCTION graphid_btree_cmp(graphid, graphid)
RETURNS int
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- sort support
CREATE FUNCTION graphid_btree_sort(internal)
RETURNS void
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- define operator classes for graphid
--

-- B-tree strategies
--   1: less than
--   2: less than or equal
--   3: equal
--   4: greater than or equal
--   5: greater than
--
-- B-tree support functions
--   1: compare two keys and return an integer less than zero, zero, or greater
--      than zero, indicating whether the first key is less than, equal to, or
--      greater than the second
--   2: return the addresses of C-callable sort support function(s) (optional)
--   3: compare a test value to a base value plus/minus an offset, and return
--      true or false according to the comparison result (optional)
CREATE OPERATOR CLASS graphid_ops DEFAULT FOR TYPE graphid USING btree AS
  OPERATOR 1 <,
  OPERATOR 2 <=,
  OPERATOR 3 =,
  OPERATOR 4 >=,
  OPERATOR 5 >,
  FUNCTION 1 graphid_btree_cmp (graphid, graphid),
  FUNCTION 2 graphid_btree_sort (internal);

--
-- graphid functions
--

CREATE FUNCTION _graphid(label_id int, entry_id bigint)
RETURNS graphid
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION _label_name(graph_oid oid, graphid)
RETURNS cstring
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION _extract_label_id(graphid)
RETURNS label_id
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype type and its support functions
--

-- define agtype as a shell type first
CREATE TYPE agtype;

CREATE FUNCTION agtype_in(cstring)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_out(agtype)
RETURNS cstring
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE agtype (
  INPUT = agtype_in,
  OUTPUT = agtype_out,
  LIKE = jsonb
);

--
-- agtype - mathematical operators (+, -, *, /, %, ^)
--

CREATE FUNCTION agtype_add(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = agtype_add,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = +
);

CREATE FUNCTION agtype_sub(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = agtype_sub,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION agtype_neg(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = agtype_neg,
  RIGHTARG = agtype
);

CREATE FUNCTION agtype_mul(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = agtype_mul,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = *
);

CREATE FUNCTION agtype_div(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = agtype_div,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION agtype_mod(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = agtype_mod,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION agtype_pow(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ^ (
  FUNCTION = agtype_pow,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

--
-- agtype - comparison operators (=, <>, <, >, <=, >=)
--

CREATE FUNCTION agtype_eq(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = agtype_eq,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION agtype_ne(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = agtype_ne,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION agtype_lt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = agtype_lt,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION agtype_gt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = agtype_gt,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION agtype_le(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = agtype_le,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION agtype_ge(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = agtype_ge,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

--
-- graph id conversion function
--
CREATE FUNCTION graphid_to_agtype(graphid)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - path
--
CREATE FUNCTION _agtype_build_path(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - vertex
--
CREATE FUNCTION _agtype_build_vertex(graphid, cstring, agtype)
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - edge
--
CREATE FUNCTION _agtype_build_edge(graphid, graphid, graphid, cstring, agtype)
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION _ag_enforce_edge_uniqueness(VARIADIC "any")
RETURNS bool
LANGUAGE c
STABLE
PARALLEL SAFE
as 'MODULE_PATHNAME';

--
-- agtype - map literal (`{key: expr, ...}`)
--

CREATE FUNCTION agtype_build_map(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_build_map()
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME', 'agtype_build_map_noargs';

--
-- There are times when the optimizer might eliminate
-- functions we need. Wrap the function with this to
-- prevent that from happening
--
CREATE FUNCTION agtype_volatile_wrapper(agt agtype)
RETURNS agtype AS $return_value$
BEGIN
	RETURN agt;
END;
$return_value$ LANGUAGE plpgsql
VOLATILE
CALLED ON NULL INPUT
PARALLEL SAFE;

--
-- agtype - list literal (`[expr, ...]`)
--

CREATE FUNCTION agtype_build_list(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_build_list()
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME', 'agtype_build_list_noargs';

--
-- agtype - type coercions
--

-- agtype -> boolean (implicit)

CREATE FUNCTION agtype_to_bool(agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS boolean)
WITH FUNCTION agtype_to_bool(agtype)
AS IMPLICIT;

-- boolean -> agtype (explicit)

CREATE FUNCTION bool_to_agtype(boolean)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (boolean AS agtype)
WITH FUNCTION bool_to_agtype(boolean);

--
-- agtype - access operators
--

-- for series of `map.key` and `container[expr]`
CREATE FUNCTION agtype_access_operator(VARIADIC agtype[])
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_access_slice(agtype, agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_in_operator(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - string matching (`STARTS WITH`, `ENDS WITH`, `CONTAINS`)
--

CREATE FUNCTION agtype_string_match_starts_with(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_string_match_ends_with(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_string_match_contains(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- functions for updating clauses
--

-- This function is defined as a VOLATILE function to prevent the optimizer
-- from pulling up Query's for CREATE clauses.
CREATE FUNCTION _cypher_create_clause(internal)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

--
-- query functions
--
CREATE FUNCTION cypher(graph_name name, query_string cstring,
                       params agtype = NULL)
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
-- Scalar Functions
--
CREATE FUNCTION id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION start_id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION end_id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION head(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION last(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION properties(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION startnode(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION endnode(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION length(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION toboolean(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION tofloat(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION tointeger(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION size(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION type(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- function for typecasting an agtype value to another agtype value
--

CREATE FUNCTION agtype_typecast_numeric(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_typecast_float(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_typecast_vertex(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_typecast_edge(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_typecast_path(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- End
--
