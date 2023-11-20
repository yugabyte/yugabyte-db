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

--
-- function for typecasting an agtype value to another agtype value
--
CREATE FUNCTION ag_catalog.agtype_typecast_int(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_numeric(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_float(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_bool(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_vertex(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_edge(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_typecast_path(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- original VLE function definition
CREATE FUNCTION ag_catalog.age_vle(IN agtype, IN agtype, IN agtype, IN agtype,
                                   IN agtype, IN agtype, IN agtype,
                                   OUT edges agtype)
    RETURNS SETOF agtype
LANGUAGE C
STABLE
CALLED ON NULL INPUT
PARALLEL UNSAFE -- might be safe
AS 'MODULE_PATHNAME';

-- This is an overloaded function definition to allow for the VLE local context
-- caching mechanism to coexist with the previous VLE version.
CREATE FUNCTION ag_catalog.age_vle(IN agtype, IN agtype, IN agtype, IN agtype,
                                   IN agtype, IN agtype, IN agtype, IN agtype,
                                   OUT edges agtype)
    RETURNS SETOF agtype
LANGUAGE C
STABLE
CALLED ON NULL INPUT
PARALLEL UNSAFE -- might be safe
AS 'MODULE_PATHNAME';

-- function to build an edge for a VLE match
CREATE FUNCTION ag_catalog.age_build_vle_match_edge(agtype, agtype)
    RETURNS agtype
    LANGUAGE C
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- function to match a terminal vle edge
CREATE FUNCTION ag_catalog.age_match_vle_terminal_edge(variadic "any")
    RETURNS boolean
    LANGUAGE C
    STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- function to create an AGTV_PATH from a VLE_path_container
CREATE FUNCTION ag_catalog.age_materialize_vle_path(agtype)
    RETURNS agtype
    LANGUAGE C
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- function to create an AGTV_ARRAY of edges from a VLE_path_container
CREATE FUNCTION ag_catalog.age_materialize_vle_edges(agtype)
    RETURNS agtype
    LANGUAGE C
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_match_vle_edge_to_id_qual(variadic "any")
    RETURNS boolean
    LANGUAGE C
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_match_two_vle_edges(agtype, agtype)
    RETURNS boolean
    LANGUAGE C
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- list functions
CREATE FUNCTION ag_catalog.age_keys(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_labels(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_nodes(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_relationships(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_range(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_unnest(agtype)
    RETURNS SETOF agtype
LANGUAGE c
IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_vertex_stats(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_delete_global_graphs(agtype)
    RETURNS boolean
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.create_complete_graph(graph_name name, nodes int,
                                                 edge_label name,
                                                 node_label name = NULL)
    RETURNS void
    LANGUAGE c
    CALLED ON NULL INPUT
    PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_create_barbell_graph(graph_name name,
                                                    graph_size int,
                                                    bridge_size int,
                                                    node_label name = NULL,
                                                    node_properties agtype = NULL,
                                                    edge_label name = NULL,
                                                    edge_properties agtype = NULL)
    RETURNS void
    LANGUAGE c
    CALLED ON NULL INPUT
    PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_prepare_cypher(cstring, cstring)
    RETURNS boolean
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- End
--
