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
\echo Use "CREATE EXTENSION agensgraph" to load this file. \quit

--
-- catalog tables
--

CREATE TABLE ag_graph (
  name name NOT NULL,
  namespace regnamespace NOT NULL
) WITH (OIDS);

CREATE UNIQUE INDEX ag_graph_oid_index ON ag_graph USING btree (oid);

CREATE UNIQUE INDEX ag_graph_name_index ON ag_graph USING btree (name);

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
-- graphid type
--

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
-- agtype type and support functions
--

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
-- agtype operator functions
--

CREATE FUNCTION agtype_add(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_sub(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_neg(agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_mul(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_div(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_mod(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_pow(agtype, agtype)
RETURNS agtype
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype operator definitions
--

CREATE OPERATOR + (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_add,
  COMMUTATOR = +
);

CREATE OPERATOR - (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_sub
);

CREATE OPERATOR - (
  RIGHTARG = agtype,
  FUNCTION = agtype_neg
);

CREATE OPERATOR * (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_mul,
  COMMUTATOR = *
);

CREATE OPERATOR / (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_div
);

CREATE OPERATOR % (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_mod
);

CREATE OPERATOR ^ (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_pow
);

--
-- agtype comparator functions
--

CREATE FUNCTION agtype_eq(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_ne(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_lt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_gt(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_le(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_ge(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype comparator definitions
--

CREATE OPERATOR = (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_eq
);

CREATE OPERATOR <> (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_ne
);

CREATE OPERATOR < (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_lt
);

CREATE OPERATOR > (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_gt
);

CREATE OPERATOR <= (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_le
);

CREATE OPERATOR >= (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = agtype_ge
);

--
-- agtype map literal functions
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
-- agtype list literal functions
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
-- agtype (boolean) <-to-> boolean functions and casts
--

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
-- agtype access operator [element], ["field"], object.field
--

CREATE FUNCTION agtype_access_operator(VARIADIC agtype[])
RETURNS agtype
LANGUAGE C
STABLE
STRICT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype string match function
--

CREATE FUNCTION agtype_string_match_starts_with(agtype, agtype)
RETURNS agtype
LANGUAGE C
STABLE
STRICT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_string_match_ends_with(agtype, agtype)
RETURNS agtype
LANGUAGE C
STABLE
STRICT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION agtype_string_match_contains(agtype, agtype)
RETURNS agtype
LANGUAGE C
STABLE
STRICT
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
-- End of agensgraph--0.0.0.sql
--
