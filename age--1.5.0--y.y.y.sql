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

-- This is a template for upgrading from the previous version of Apache AGE
-- It will only work within versions of PostgreSQL, not across.
-- Please adjust the below ALTER EXTENSION to reflect the correct version
-- it is upgrading to.

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION age UPDATE TO '1.4.0'" to load this file. \quit

-- Please add all additions, deletions, and modifications to the end of this
-- file. We need to keep the order of these changes.

DROP FUNCTION IF EXISTS ag_catalog.load_labels_from_file(name, name, text, bool);
CREATE FUNCTION ag_catalog.load_labels_from_file(graph_name name,
                                                 label_name name,
                                                 file_path text,
                                                 id_field_exists bool default true,
                                                 load_as_agtype bool default false)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

DROP FUNCTION IF EXISTS ag_catalog.load_edges_from_file(name, name, text);
CREATE FUNCTION ag_catalog.load_edges_from_file(graph_name name,
                                                label_name name,
                                                file_path text,
                                                load_as_agtype bool default false)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_contains_top_level(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR @>> (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = ag_catalog.agtype_contains_top_level,
  COMMUTATOR = '<<@',
  RESTRICT = contsel,
  JOIN = contjoinsel
);

CREATE FUNCTION ag_catalog.agtype_contained_by_top_level(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <<@ (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = ag_catalog.agtype_contained_by_top_level,
  COMMUTATOR = '@>>',
  RESTRICT = contsel,
  JOIN = contjoinsel
);

/*
 * Since there is no option to add or drop operator from class,
 * we have to drop and recreate the whole operator class.
 * Reference: https://www.postgresql.org/docs/current/sql-alteropclass.html
 */

DROP OPERATOR CLASS ag_catalog.gin_agtype_ops;

CREATE OPERATOR CLASS ag_catalog.gin_agtype_ops
DEFAULT FOR TYPE agtype USING gin AS
  OPERATOR 7 @>(agtype, agtype),
  OPERATOR 8 <@(agtype, agtype),
  OPERATOR 9 ?(agtype, agtype),
  OPERATOR 10 ?|(agtype, agtype),
  OPERATOR 11 ?&(agtype, agtype),
  OPERATOR 12 @>>(agtype, agtype),
  OPERATOR 13 <<@(agtype, agtype),
  FUNCTION 1 ag_catalog.gin_compare_agtype(text,text),
  FUNCTION 2 ag_catalog.gin_extract_agtype(agtype, internal),
  FUNCTION 3 ag_catalog.gin_extract_agtype_query(agtype, internal, int2,
                                                 internal, internal),
  FUNCTION 4 ag_catalog.gin_consistent_agtype(internal, int2, agtype, int4,
                                              internal, internal),
  FUNCTION 6 ag_catalog.gin_triconsistent_agtype(internal, int2, agtype, int4,
                                                 internal, internal, internal),
STORAGE text;

-- this function went from variadic "any" to just "any" type
CREATE OR REPLACE FUNCTION ag_catalog.age_tostring("any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- this is a new function for graph statistics
CREATE FUNCTION ag_catalog.age_graph_stats(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.graph_exists(graph_name name)
    RETURNS agtype
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'age_graph_exists';

CREATE FUNCTION ag_catalog.age_is_valid_label_name(agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OR REPLACE FUNCTION ag_catalog.create_vlabel(graph_name cstring, label_name cstring)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE OR REPLACE FUNCTION ag_catalog.create_elabel(graph_name cstring, label_name cstring)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_to_json(agtype)
    RETURNS json
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS json)
    WITH FUNCTION ag_catalog.agtype_to_json(agtype);