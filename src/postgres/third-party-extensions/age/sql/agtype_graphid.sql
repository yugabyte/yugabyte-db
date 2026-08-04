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
-- graph id conversion function
--
CREATE FUNCTION ag_catalog.graphid_to_agtype(graphid)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (graphid AS agtype)
    WITH FUNCTION ag_catalog.graphid_to_agtype(graphid);

CREATE FUNCTION ag_catalog.agtype_to_graphid(agtype)
    RETURNS graphid
    LANGUAGE c
    IMMUTABLE
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
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - vertex
--
CREATE FUNCTION ag_catalog._agtype_build_vertex(graphid, cstring, agtype)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - edge
--
CREATE FUNCTION ag_catalog._agtype_build_edge(graphid, graphid, graphid,
                                              cstring, agtype)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
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
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_build_map()
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME', 'agtype_build_map_noargs';

CREATE FUNCTION ag_catalog.agtype_build_map_nonull(VARIADIC "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- There are times when the optimizer might eliminate
-- functions we need. Wrap the function with this to
-- prevent that from happening
--

CREATE FUNCTION ag_catalog.agtype_volatile_wrapper("any")
    RETURNS agtype
    LANGUAGE c
    VOLATILE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

--
-- agtype - list literal (`[expr, ...]`)
--

CREATE FUNCTION ag_catalog.agtype_build_list(VARIADIC "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_build_list()
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
CALLED ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME', 'agtype_build_list_noargs';

