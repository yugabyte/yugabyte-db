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
-- agtype type and its support functions
--

-- define agtype as a shell type first
CREATE TYPE agtype;

CREATE FUNCTION ag_catalog.agtype_in(cstring)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_out(agtype)
    RETURNS cstring
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

-- binary I/O functions
CREATE FUNCTION ag_catalog.agtype_send(agtype)
    RETURNS bytea
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE FUNCTION ag_catalog.agtype_recv(internal)
    RETURNS agtype
    LANGUAGE c
    IMMUTABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE TYPE agtype (
  INPUT = ag_catalog.agtype_in,
  OUTPUT = ag_catalog.agtype_out,
  SEND = ag_catalog.agtype_send,
  RECEIVE = ag_catalog.agtype_recv,
  LIKE = jsonb
);

--
-- agtype - mathematical operators (+, -, *, /, %, ^)
--

CREATE FUNCTION ag_catalog.agtype_add(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_add,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, smallint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  smallint,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(smallint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = smallint,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, integer)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  integer,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(integer, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = integer,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, bigint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  bigint,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(bigint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = bigint,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, real)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  real,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(real, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = real,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, double precision)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  double precision,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(double precision, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = double precision,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(agtype, numeric)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = agtype,
  RIGHTARG =  numeric,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_any_add(numeric, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR + (
  FUNCTION = ag_catalog.agtype_any_add,
  LEFTARG = numeric,
  RIGHTARG =  agtype,
  COMMUTATOR = +
);

CREATE FUNCTION ag_catalog.agtype_sub(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_sub,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, smallint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  smallint
);

CREATE FUNCTION ag_catalog.agtype_any_sub(smallint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = smallint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, integer)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  integer
);

CREATE FUNCTION ag_catalog.agtype_any_sub(integer, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = integer,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, bigint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  bigint
);

CREATE FUNCTION ag_catalog.agtype_any_sub(bigint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = bigint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, real)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  real
);

CREATE FUNCTION ag_catalog.agtype_any_sub(real, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = real,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, double precision)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  double precision
);

CREATE FUNCTION ag_catalog.agtype_any_sub(double precision, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = double precision,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_sub(agtype, numeric)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = agtype,
  RIGHTARG =  numeric
);

CREATE FUNCTION ag_catalog.agtype_any_sub(numeric, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_any_sub,
  LEFTARG = numeric,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_neg(agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR - (
  FUNCTION = ag_catalog.agtype_neg,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_mul(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_mul,
  LEFTARG = agtype,
  RIGHTARG = agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, smallint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  smallint,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(smallint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = smallint,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, integer)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  integer,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(integer, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = integer,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, bigint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  bigint,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(bigint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = bigint,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, real)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  real,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(real, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = real,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, double precision)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  double precision,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(double precision, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = double precision,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(agtype, numeric)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = agtype,
  RIGHTARG =  numeric,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_any_mul(numeric, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR * (
  FUNCTION = ag_catalog.agtype_any_mul,
  LEFTARG = numeric,
  RIGHTARG =  agtype,
  COMMUTATOR = *
);

CREATE FUNCTION ag_catalog.agtype_div(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_div,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, smallint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  smallint
);

CREATE FUNCTION ag_catalog.agtype_any_div(smallint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = smallint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, integer)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  integer
);

CREATE FUNCTION ag_catalog.agtype_any_div(integer, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = integer,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, bigint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  bigint
);

CREATE FUNCTION ag_catalog.agtype_any_div(bigint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = bigint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, real)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  real
);

CREATE FUNCTION ag_catalog.agtype_any_div(real, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = real,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, double precision)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  double precision
);

CREATE FUNCTION ag_catalog.agtype_any_div(double precision, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = double precision,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_div(agtype, numeric)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = agtype,
  RIGHTARG =  numeric
);

CREATE FUNCTION ag_catalog.agtype_any_div(numeric, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR / (
  FUNCTION = ag_catalog.agtype_any_div,
  LEFTARG = numeric,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_mod(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_mod,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, smallint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  smallint
);

CREATE FUNCTION ag_catalog.agtype_any_mod(smallint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = smallint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, integer)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  integer
);

CREATE FUNCTION ag_catalog.agtype_any_mod(integer, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = integer,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, bigint)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  bigint
);

CREATE FUNCTION ag_catalog.agtype_any_mod(bigint, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = bigint,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, real)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  real
);

CREATE FUNCTION ag_catalog.agtype_any_mod(real, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = real,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, double precision)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  double precision
);

CREATE FUNCTION ag_catalog.agtype_any_mod(double precision, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = double precision,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_any_mod(agtype, numeric)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = agtype,
  RIGHTARG =  numeric
);

CREATE FUNCTION ag_catalog.agtype_any_mod(numeric, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR % (
  FUNCTION = ag_catalog.agtype_any_mod,
  LEFTARG = numeric,
  RIGHTARG =  agtype
);

CREATE FUNCTION ag_catalog.agtype_pow(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR ^ (
  FUNCTION = ag_catalog.agtype_pow,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.agtype_concat(agtype, agtype)
    RETURNS agtype
    LANGUAGE c
    STABLE
RETURNS NULL ON NULL INPUT
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR || (
  FUNCTION = ag_catalog.agtype_concat,
  LEFTARG = agtype,
  RIGHTARG = agtype
);

CREATE FUNCTION ag_catalog.graphid_hash_cmp(graphid)
    RETURNS INTEGER
    LANGUAGE c
    STABLE
PARALLEL SAFE
AS 'MODULE_PATHNAME';

CREATE OPERATOR CLASS graphid_ops_hash
  DEFAULT
  FOR TYPE graphid
  USING hash AS
  OPERATOR 1 =,
  FUNCTION 1 ag_catalog.graphid_hash_cmp(graphid);

