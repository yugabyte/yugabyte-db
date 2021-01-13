/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
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

CREATE FUNCTION ag_catalog._label_id(graph_name name, label_name name)
RETURNS label_id
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- utility functions
--

CREATE FUNCTION ag_catalog.create_graph(graph_name name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.drop_graph(graph_name name, cascade boolean = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.alter_graph(graph_name name, operation cstring, new_value name)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.drop_label(graph_name name, label_name name,
                           force boolean = false)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

--
-- graphid type
--

-- define graphid as a shell type first
CREATE TYPE graphid;

CREATE FUNCTION ag_catalog.graphid_in(cstring)
RETURNS graphid
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.graphid_out(graphid)
RETURNS cstring
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE graphid (
  INPUT = ag_catalog.graphid_in,
  OUTPUT = ag_catalog.graphid_out,
  INTERNALLENGTH = 8,
  PASSEDBYVALUE,
  ALIGNMENT = float8,
  STORAGE = plain
);

--
-- graphid - comparison operators (=, <>, <, >, <=, >=)
--

CREATE FUNCTION ag_catalog.graphid_eq(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.graphid_eq,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.graphid_ne(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.graphid_ne,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.graphid_lt(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.graphid_lt,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.graphid_gt(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.graphid_gt,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.graphid_le(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.graphid_le,
  LEFTARG = graphid,
  RIGHTARG = graphid,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.graphid_ge(graphid, graphid)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.graphid_ge,
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
CREATE FUNCTION ag_catalog.graphid_btree_cmp(graphid, graphid)
RETURNS int
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- sort support
CREATE FUNCTION ag_catalog.graphid_btree_sort(internal)
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
  FUNCTION 1 ag_catalog.graphid_btree_cmp (graphid, graphid),
  FUNCTION 2 ag_catalog.graphid_btree_sort (internal);

--
-- graphid functions
--

CREATE FUNCTION ag_catalog._graphid(label_id int, entry_id bigint)
RETURNS graphid
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._label_name(graph_oid oid, graphid)
RETURNS cstring
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._extract_label_id(graphid)
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

CREATE FUNCTION ag_catalog.agtype_in(cstring)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_out(agtype)
RETURNS cstring
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE agtype (
  INPUT = ag_catalog.agtype_in,
  OUTPUT = ag_catalog.agtype_out,
  LIKE = jsonb
);

--
-- agtype - mathematical operators (+, -, *, /, %, ^)
--

CREATE FUNCTION ag_catalog.agtype_add(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_add,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_sub(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_sub,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_neg(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_neg,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_mul(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_mul,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_div(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_div,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_mod(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_mod,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_pow(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ^ (
  FUNCTION = ag_catalog.agtype_pow,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

--
-- agtype - comparison operators (=, <>, <, >, <=, >=)
--

CREATE FUNCTION ag_catalog.agtype_eq(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_eq,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_ne(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_ne,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_lt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_lt,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_gt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_gt,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_le(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_le,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_ge(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_ge,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_btree_cmp(agtype, agtype)
RETURNS INTEGER
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR CLASS agtype_ops_btree
  DEFAULT
  FOR TYPE agtype
  USING btree AS
  OPERATOR 1 <,
  OPERATOR 2 <=,
  OPERATOR 3 =,
  OPERATOR 4 >,
  OPERATOR 5 >=,
  FUNCTION 1 ag_catalog.agtype_btree_cmp(agtype, agtype);

CREATE FUNCTION ag_catalog.agtype_hash_cmp(agtype)
RETURNS INTEGER
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR CLASS agtype_ops_hash
  DEFAULT
  FOR TYPE agtype
  USING hash AS
  OPERATOR 1 =,
  FUNCTION 1 ag_catalog.agtype_hash_cmp(agtype);

--
-- graph id conversion function
--
CREATE FUNCTION ag_catalog.graphid_to_agtype(graphid)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (graphid AS agtype)
WITH FUNCTION ag_catalog.graphid_to_agtype(graphid);

CREATE FUNCTION ag_catalog.agtype_to_graphid(agtype)
RETURNS graphid
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS graphid)
WITH FUNCTION ag_catalog.agtype_to_graphid(agtype)
AS IMPLICIT;


--
-- agtype - path
--
CREATE FUNCTION ag_catalog._agtype_build_path(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - vertex
--
CREATE FUNCTION ag_catalog._agtype_build_vertex(graphid, cstring, agtype)
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - edge
--
CREATE FUNCTION ag_catalog._agtype_build_edge(graphid, graphid, graphid, cstring, agtype)
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._ag_enforce_edge_uniqueness(VARIADIC "any")
RETURNS bool
LANGUAGE c
STABLE
PARALLEL SAFE
as 'MODULE_PATHNAME';

--
-- agtype - map literal (`{key: expr, ...}`)
--

CREATE FUNCTION ag_catalog.agtype_build_map(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_build_map()
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
CREATE FUNCTION ag_catalog.agtype_volatile_wrapper(agt agtype)
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

CREATE FUNCTION ag_catalog.agtype_build_list(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_build_list()
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
CREATE FUNCTION ag_catalog.agtype_to_bool(agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS boolean)
WITH FUNCTION ag_catalog.agtype_to_bool(agtype)
AS IMPLICIT;

-- boolean -> agtype (explicit)
CREATE FUNCTION ag_catalog.bool_to_agtype(boolean)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (boolean AS agtype)
WITH FUNCTION ag_catalog.bool_to_agtype(boolean);

-- float8 -> agtype (explicit)
CREATE FUNCTION ag_catalog.float8_to_agtype(float8)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (float8 AS agtype)
WITH FUNCTION ag_catalog.float8_to_agtype(float8);

-- agtype -> float8 (implicit)
CREATE FUNCTION ag_catalog.agtype_to_float8(agtype)
RETURNS float8
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS float8)
WITH FUNCTION ag_catalog.agtype_to_float8(agtype)
AS IMPLICIT;

-- int8 -> agtype (explicit)
CREATE FUNCTION ag_catalog.int8_to_agtype(int8)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (int8 AS agtype)
WITH FUNCTION ag_catalog.int8_to_agtype(int8);

--
-- agtype - access operators
--

-- for series of `map.key` and `container[expr]`
CREATE FUNCTION ag_catalog.agtype_access_operator(VARIADIC agtype[])
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_access_slice(agtype, agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_in_operator(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - string matching (`STARTS WITH`, `ENDS WITH`, `CONTAINS`)
--

CREATE FUNCTION ag_catalog.agtype_string_match_starts_with(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_string_match_ends_with(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_string_match_contains(agtype, agtype)
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
CREATE FUNCTION ag_catalog._cypher_create_clause(internal)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._cypher_set_clause(internal)
RETURNS void
LANGUAGE c
AS 'MODULE_PATHNAME';

--
-- query functions
--
CREATE FUNCTION ag_catalog.cypher(graph_name name, query_string cstring,
                       params agtype = NULL)
RETURNS SETOF record
LANGUAGE c
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.get_cypher_keywords(OUT word text, OUT catcode "char",
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
CREATE FUNCTION ag_catalog.age_id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_start_id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_end_id(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_head(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_last(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_properties(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_startnode(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_endnode(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_length(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_toboolean(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tofloat(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tointeger(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tostring(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_size(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_type(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_exists(agtype)
RETURNS boolean
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._property_constraint_check(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- String functions
--
CREATE FUNCTION ag_catalog.age_reverse(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_toupper(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tolower(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_ltrim(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_rtrim(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_trim(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_right(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_left(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_substring(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_split(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_replace(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- Trig functions - radian input
--
CREATE FUNCTION ag_catalog.age_sin(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_cos(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tan(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_cot(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_asin(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_acos(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_atan(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_atan2(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_degrees(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_radians(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_round(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_ceil(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_floor(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_abs(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_sign(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_log(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_log10(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_e()
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_exp(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_sqrt(variadic "any")
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_timestamp()
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- aggregate functions components for stdev & stdevp
--

-- wrapper for the stdev final function to pass 0 instead of null
CREATE FUNCTION ag_catalog.age_float8_stddev_samp_aggfinalfn(_float8)
RETURNS float8
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
-- aggregate for age_stdev
CREATE AGGREGATE ag_catalog.age_stdev(float8)
(
   stype = _float8,
   sfunc = float8_accum,
   finalfunc = ag_catalog.age_float8_stddev_samp_aggfinalfn,
   combinefunc = float8_combine,
   finalfunc_modify = read_only,
   initcond = '{0,0,0}',
   parallel = safe
);
-- wrapper for the stdevp final function to pass 0 instead of null
CREATE FUNCTION ag_catalog.age_float8_stddev_pop_aggfinalfn(_float8)
RETURNS float8
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
-- aggregate for age_stdevp
CREATE AGGREGATE ag_catalog.age_stdevp (float8)
(
   stype = _float8,
   sfunc = float8_accum,
   finalfunc = ag_catalog.age_float8_stddev_pop_aggfinalfn,
   combinefunc = float8_combine,
   finalfunc_modify = read_only,
   initcond = '{0,0,0}',
   parallel = safe
);

--
-- aggregate transfer functions for min & max
--
-- max
CREATE FUNCTION ag_catalog.age_agtype_larger_aggtransfn(agtype, agtype)
RETURNS agtype
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
-- aggregate for max
CREATE AGGREGATE ag_catalog.age_max (agtype)
(
   stype = agtype,
   sfunc = ag_catalog.age_agtype_larger_aggtransfn,
   combinefunc = ag_catalog.age_agtype_larger_aggtransfn,
   finalfunc_modify = read_only,
   sortop = >,
   parallel = safe
);
-- min
CREATE FUNCTION ag_catalog.age_agtype_smaller_aggtransfn(agtype, agtype)
RETURNS agtype
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
-- aggregate for min
CREATE AGGREGATE ag_catalog.age_min (agtype)
(
   stype = agtype,
   sfunc = ag_catalog.age_agtype_smaller_aggtransfn,
   combinefunc = ag_catalog.age_agtype_smaller_aggtransfn,
   finalfunc_modify = read_only,
   sortop = <,
   parallel = safe
);

--
-- aggregate transfer/final functions for percentileCont & percentileDisc
--
CREATE FUNCTION ag_catalog.age_percentile_aggtransfn(internal, float8, float8)
RETURNS internal
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_percentile_cont_aggfinalfn(internal)
RETURNS float8
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_percentile_disc_aggfinalfn(internal)
RETURNS float8
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE AGGREGATE ag_catalog.age_percentilecont(float8, float8)
(
    stype = internal,
    sfunc = ag_catalog.age_percentile_aggtransfn,
    finalfunc = ag_catalog.age_percentile_cont_aggfinalfn,
    parallel = safe
);

CREATE AGGREGATE ag_catalog.age_percentiledisc(float8, float8)
(
    stype = internal,
    sfunc = ag_catalog.age_percentile_aggtransfn,
    finalfunc = ag_catalog.age_percentile_disc_aggfinalfn,
    parallel = safe
);

--
-- function for typecasting an agtype value to another agtype value
--
CREATE FUNCTION ag_catalog.agtype_typecast_numeric(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_float(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_vertex(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_edge(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_path(agtype)
RETURNS agtype
LANGUAGE c
STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- End
--
