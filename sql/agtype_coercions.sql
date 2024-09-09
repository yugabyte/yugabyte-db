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
-- agtype - type coercions
--
-- agtype -> text (explicit)
CREATE FUNCTION ag_catalog.agtype_to_text(agtype)
    RETURNS text
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS text)
    WITH FUNCTION ag_catalog.agtype_to_text(agtype);

-- agtype -> boolean (implicit)
CREATE FUNCTION ag_catalog.agtype_to_bool(agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (boolean AS agtype)
    WITH FUNCTION ag_catalog.bool_to_agtype(boolean);

-- float8 -> agtype (explicit)
CREATE FUNCTION ag_catalog.float8_to_agtype(float8)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (float8 AS agtype)
    WITH FUNCTION ag_catalog.float8_to_agtype(float8);

-- agtype -> float8 (implicit)
CREATE FUNCTION ag_catalog.agtype_to_float8(agtype)
    RETURNS float8
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS float8)
    WITH FUNCTION ag_catalog.agtype_to_float8(agtype);

-- int8 -> agtype (explicit)
CREATE FUNCTION ag_catalog.int8_to_agtype(int8)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (int8 AS agtype)
    WITH FUNCTION ag_catalog.int8_to_agtype(int8);

-- agtype -> int8
CREATE FUNCTION ag_catalog.agtype_to_int8(variadic "any")
    RETURNS bigint
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS bigint)
    WITH FUNCTION ag_catalog.agtype_to_int8(variadic "any")
AS ASSIGNMENT;

-- agtype -> int4
CREATE FUNCTION ag_catalog.agtype_to_int4(variadic "any")
    RETURNS int
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS int)
    WITH FUNCTION ag_catalog.agtype_to_int4(variadic "any");

-- agtype -> int2
CREATE FUNCTION ag_catalog.agtype_to_int2(variadic "any")
    RETURNS smallint
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS smallint)
    WITH FUNCTION ag_catalog.agtype_to_int2(variadic "any");

-- agtype -> int4[]
CREATE FUNCTION ag_catalog.agtype_to_int4_array(variadic "any")
    RETURNS int[]
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS int[])
    WITH FUNCTION ag_catalog.agtype_to_int4_array(variadic "any");

CREATE FUNCTION ag_catalog.agtype_to_json(agtype)
    RETURNS json
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE CAST (agtype AS json)
    WITH FUNCTION ag_catalog.agtype_to_json(agtype);