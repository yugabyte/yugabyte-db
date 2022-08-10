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
\echo Use "ALTER EXTENSION age UPDATE TO '1.1.0'" to load this file. \quit

--
-- agtype - access operators ( ->, ->> )
--

CREATE FUNCTION ag_catalog.agtype_object_field(agtype, text)
RETURNS agtype
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- get agtype object field
CREATE OPERATOR -> (
  LEFTARG = agtype,
  RIGHTARG = text,
  FUNCTION = ag_catalog.agtype_object_field
);

CREATE FUNCTION ag_catalog.agtype_object_field_text(agtype, text)
RETURNS text
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- get agtype object field as text
CREATE OPERATOR ->> (
  LEFTARG = agtype,
  RIGHTARG = text,
  FUNCTION = ag_catalog.agtype_object_field_text
);

CREATE FUNCTION ag_catalog.agtype_array_element(agtype, int4)
RETURNS agtype
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- get agtype array element
CREATE OPERATOR -> (
  LEFTARG = agtype,
  RIGHTARG = int4,
  FUNCTION = ag_catalog.agtype_array_element
);

CREATE FUNCTION ag_catalog.agtype_array_element_text(agtype, int4)
RETURNS text
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- get agtype array element as text
CREATE OPERATOR ->> (
  LEFTARG = agtype,
  RIGHTARG = int4,
  FUNCTION = ag_catalog.agtype_array_element_text
);

--
-- Contains operators @> <@
--
CREATE FUNCTION ag_catalog.agtype_contains(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR @> (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = ag_catalog.agtype_contains,
  COMMUTATOR = '<@',
  RESTRICT = contsel,
  JOIN = contjoinsel
);

CREATE FUNCTION ag_catalog.agtype_contained_by(agtype, agtype)
RETURNS boolean
LANGUAGE c
STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <@ (
  LEFTARG = agtype,
  RIGHTARG = agtype,
  FUNCTION = ag_catalog.agtype_contained_by,
  COMMUTATOR = '@>',
  RESTRICT = contsel,
  JOIN = contjoinsel
);

--
-- Key Existence Operators ? ?| ?&
--
CREATE FUNCTION ag_catalog.agtype_exists(agtype, text)
RETURNS boolean
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ? (
  LEFTARG = agtype,
  RIGHTARG = text,
  FUNCTION = ag_catalog.agtype_exists,
  COMMUTATOR = '?',
  RESTRICT = contsel,
  JOIN = contjoinsel
);

CREATE FUNCTION ag_catalog.agtype_exists_any(agtype, text[])
RETURNS boolean
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ?| (
  LEFTARG = agtype,
  RIGHTARG = text[],
  FUNCTION = ag_catalog.agtype_exists_any,
  RESTRICT = contsel,
  JOIN = contjoinsel
);

CREATE FUNCTION ag_catalog.agtype_exists_all(agtype, text[])
RETURNS boolean
LANGUAGE c
IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ?& (
  LEFTARG = agtype,
  RIGHTARG = text[],
  FUNCTION = ag_catalog.agtype_exists_all,
  RESTRICT = contsel,
  JOIN = contjoinsel
);

--
-- agtype GIN support
--
CREATE FUNCTION ag_catalog.gin_compare_agtype(text, text)
RETURNS int
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION gin_extract_agtype(agtype, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_extract_agtype_query(agtype, internal, int2,
                                                    internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_consistent_agtype(internal, int2, agtype, int4,
                                                 internal, internal)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE FUNCTION ag_catalog.gin_triconsistent_agtype(internal, int2, agtype, int4,
                                                    internal, internal, internal)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C
IMMUTABLE
STRICT
PARALLEL SAFE;

CREATE OPERATOR CLASS ag_catalog.gin_agtype_ops
DEFAULT FOR TYPE agtype USING gin AS
  OPERATOR 7 @>,
  OPERATOR 9 ?(agtype, text),
  OPERATOR 10 ?|(agtype, text[]),
  OPERATOR 11 ?&(agtype, text[]),
  FUNCTION 1 ag_catalog.gin_compare_agtype(text,text),
  FUNCTION 2 ag_catalog.gin_extract_agtype(agtype, internal),
  FUNCTION 3 ag_catalog.gin_extract_agtype_query(agtype, internal, int2,
                                                 internal, internal),
  FUNCTION 4 ag_catalog.gin_consistent_agtype(internal, int2, agtype, int4,
                                              internal, internal),
  FUNCTION 6 ag_catalog.gin_triconsistent_agtype(internal, int2, agtype, int4,
                                                 internal, internal, internal),
STORAGE text;

--
-- graph id conversion function
--
ALTER FUNCTION ag_catalog.agtype_access_operator(VARIADIC agtype[]) IMMUTABLE;

DROP FUNCTION IF EXISTS ag_catalog._property_constraint_check(agtype, agtype);

--
-- end
--
