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
-- agtype - comparison operators (=, <>, <, >, <=, >=)
--

CREATE FUNCTION ag_catalog.agtype_eq(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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
  JOIN = eqjoinsel,
  HASHES
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_eq(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR = (
  FUNCTION = ag_catalog.agtype_any_eq,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_ne(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ne(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <> (
  FUNCTION = ag_catalog.agtype_any_ne,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel
);

CREATE FUNCTION ag_catalog.agtype_lt(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_lt(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR < (
  FUNCTION = ag_catalog.agtype_any_lt,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel
);

CREATE FUNCTION ag_catalog.agtype_gt(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_gt(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR > (
  FUNCTION = ag_catalog.agtype_any_gt,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel
);

CREATE FUNCTION ag_catalog.agtype_le(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_le(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR <= (
  FUNCTION = ag_catalog.agtype_any_le,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarlesel,
  JOIN = scalarlejoinsel
);

CREATE FUNCTION ag_catalog.agtype_ge(agtype, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
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

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, smallint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = smallint,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(smallint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = smallint,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, integer)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = integer,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(integer, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = integer,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, bigint)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = bigint,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(bigint, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = bigint,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, real)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = real,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(real, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = real,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, double precision)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = double precision,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(double precision, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = double precision,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(agtype, numeric)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = agtype,
  RIGHTARG = numeric,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_any_ge(numeric, agtype)
    RETURNS boolean
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR >= (
  FUNCTION = ag_catalog.agtype_any_ge,
  LEFTARG = numeric,
  RIGHTARG = agtype,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargesel,
  JOIN = scalargejoinsel
);

CREATE FUNCTION ag_catalog.agtype_btree_cmp(agtype, agtype)
    RETURNS INTEGER
    LANGUAGE c
    IMMUTABLE
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

