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
\echo Use "ALTER EXTENSION age UPDATE TO '1.3.0'" to load this file. \quit

CREATE FUNCTION ag_catalog.agtype_build_map_nonull(VARIADIC "any")
RETURNS agtype
LANGUAGE c
STABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_isempty(agtype)
RETURNS boolean
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OR REPLACE FUNCTION ag_catalog.age_unnest(agtype)
RETURNS SETOF agtype
LANGUAGE c
IMMUTABLE
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

--
-- End
--
