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
-- String functions
--
CREATE FUNCTION ag_catalog.age_reverse(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_toupper(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_tolower(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_ltrim(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_rtrim(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_trim(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_right(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_left(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_substring(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_split(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.age_replace(variadic "any")
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';
