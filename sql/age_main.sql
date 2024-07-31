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
                          graphid oid NOT NULL,
                          name name NOT NULL,
                          namespace regnamespace NOT NULL
);

CREATE UNIQUE INDEX ag_graph_graphid_index ON ag_graph USING btree (graphid);

-- include content of the ag_graph table into the pg_dump output
SELECT pg_catalog.pg_extension_config_dump('ag_graph', '');

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
                          relation regclass NOT NULL,
                          seq_name name NOT NULL,
                          CONSTRAINT fk_graph_oid
                              FOREIGN KEY(graph)
                                  REFERENCES ag_graph(graphid)
);

-- include content of the ag_label table into the pg_dump output
SELECT pg_catalog.pg_extension_config_dump('ag_label', '');

CREATE UNIQUE INDEX ag_label_name_graph_index
    ON ag_label
    USING btree (name, graph);

CREATE UNIQUE INDEX ag_label_graph_oid_index
    ON ag_label
    USING btree (graph, id);

CREATE UNIQUE INDEX ag_label_relation_index ON ag_label USING btree (relation);

CREATE UNIQUE INDEX ag_label_seq_name_graph_index
    ON ag_label
    USING btree (seq_name, graph);

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

CREATE FUNCTION ag_catalog.create_vlabel(graph_name cstring, label_name cstring)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.create_elabel(graph_name cstring, label_name cstring)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.alter_graph(graph_name name, operation cstring,
                                       new_value name)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.drop_label(graph_name name, label_name name,
                                      force boolean = false)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

--
-- If `load_as_agtype` is true, property values are loaded as agtype; otherwise
-- loaded as string.
--
CREATE FUNCTION ag_catalog.load_labels_from_file(graph_name name,
                                                 label_name name,
                                                 file_path text,
                                                 id_field_exists bool default true,
                                                 load_as_agtype bool default false)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.load_edges_from_file(graph_name name,
                                                label_name name,
                                                file_path text,
                                                load_as_agtype bool default false)
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
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.graphid_out(graphid)
    RETURNS cstring
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- binary I/O functions
CREATE FUNCTION ag_catalog.graphid_send(graphid)
    RETURNS bytea
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.graphid_recv(internal)
    RETURNS graphid
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE graphid (
  INPUT = ag_catalog.graphid_in,
  OUTPUT = ag_catalog.graphid_out,
  SEND = ag_catalog.graphid_send,
  RECEIVE = ag_catalog.graphid_recv,
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
    IMMUTABLE
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
  JOIN = eqjoinsel,
  HASHES,
  MERGES
);

CREATE FUNCTION ag_catalog.graphid_ne(graphid, graphid)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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
    IMMUTABLE
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
    IMMUTABLE
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
    IMMUTABLE
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
    IMMUTABLE
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
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- sort support
CREATE FUNCTION ag_catalog.graphid_btree_sort(internal)
    RETURNS void
    LANGUAGE c
    IMMUTABLE
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
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog._extract_label_id(graphid)
    RETURNS label_id
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
