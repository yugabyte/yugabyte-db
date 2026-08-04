/* Copyright 2013 Aggregate Knowledge, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION hll" to load this file. \quit

-- ----------------------------------------------------------------
-- Type
-- ----------------------------------------------------------------

CREATE TYPE hll;

CREATE FUNCTION hll_in(cstring, oid, integer)
RETURNS hll
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_out(hll)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_recv(internal)
RETURNS hll
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION hll_send(hll)
RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION hll_typmod_in(cstring[])
RETURNS integer
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_typmod_out(integer)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll(hll, integer, boolean)
RETURNS hll
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE TYPE hll (
        INTERNALLENGTH = variable,
        INPUT = hll_in,
        OUTPUT = hll_out,
        TYPMOD_IN = hll_typmod_in,
        TYPMOD_OUT = hll_typmod_out,
        RECEIVE = hll_recv,
        SEND = hll_send,
        STORAGE = external
);

CREATE CAST (hll AS hll) WITH FUNCTION hll(hll, integer, boolean) AS IMPLICIT;

CREATE CAST (bytea AS hll) WITHOUT FUNCTION;

-- ----------------------------------------------------------------
-- Hashed value type
-- ----------------------------------------------------------------

CREATE TYPE hll_hashval;

CREATE FUNCTION hll_hashval_in(cstring, oid, integer)
RETURNS hll_hashval
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_hashval_out(hll_hashval)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE TYPE hll_hashval (
        INTERNALLENGTH = 8,
        PASSEDBYVALUE,
        ALIGNMENT = double,
        INPUT = hll_hashval_in,
        OUTPUT = hll_hashval_out
);

CREATE FUNCTION hll_hashval_eq(hll_hashval, hll_hashval)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_hashval_ne(hll_hashval, hll_hashval)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_hashval(bigint)
RETURNS hll_hashval
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_hashval_int4(integer)
RETURNS hll_hashval
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE OPERATOR = (
	LEFTARG = hll_hashval, RIGHTARG = hll_hashval,
                PROCEDURE = hll_hashval_eq,
	COMMUTATOR = '=', NEGATOR = '<>',
	RESTRICT = eqsel, JOIN = eqjoinsel,
	MERGES
);

CREATE OPERATOR <> (
	LEFTARG = hll_hashval, RIGHTARG = hll_hashval,
                PROCEDURE = hll_hashval_ne,
	COMMUTATOR = '<>', NEGATOR = '=',
	RESTRICT = neqsel, JOIN = neqjoinsel
);

-- Only allow explicit casts.
CREATE CAST (bigint AS hll_hashval) WITHOUT FUNCTION;
CREATE CAST (integer AS hll_hashval) WITH FUNCTION hll_hashval_int4(integer);

-- ----------------------------------------------------------------
-- Functions
-- ----------------------------------------------------------------

-- Equality of multisets.
--
CREATE FUNCTION hll_eq(hll, hll)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

-- Inequality of multisets.
--
CREATE FUNCTION hll_ne(hll, hll)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

-- Cardinality of a multiset.
--
CREATE FUNCTION hll_cardinality(hll)
     RETURNS double precision
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Union of a pair of multisets.
--
CREATE FUNCTION hll_union(hll, hll)
     RETURNS hll
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Adds an integer hash to a multiset.
--
CREATE FUNCTION hll_add(hll, hll_hashval)
     RETURNS hll
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Adds a multiset to an integer hash.
--
CREATE FUNCTION hll_add_rev(hll_hashval, hll)
     RETURNS hll
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Pretty-print a multiset.
--
CREATE FUNCTION hll_print(hll)
     RETURNS cstring
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Create an empty multiset with parameters.
--
-- NOTE - we create multiple signatures to avoid coding the defaults
-- in this sql file.  This allows the defaults to changed at runtime.
--
CREATE FUNCTION hll_empty()
     RETURNS hll
     AS 'MODULE_PATHNAME', 'hll_empty0'
     LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_empty(integer)
     RETURNS hll
     AS 'MODULE_PATHNAME', 'hll_empty1'
     LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_empty(integer, integer)
     RETURNS hll
     AS 'MODULE_PATHNAME', 'hll_empty2'
     LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_empty(integer, integer, bigint)
     RETURNS hll
     AS 'MODULE_PATHNAME', 'hll_empty3'
     LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION hll_empty(integer, integer, bigint, integer)
     RETURNS hll
     AS 'MODULE_PATHNAME', 'hll_empty4'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the schema version of an hll.
--
CREATE FUNCTION hll_schema_version(hll)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the type of an hll.
--
CREATE FUNCTION hll_type(hll)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the log2m value of an hll.
--
CREATE FUNCTION hll_log2m(hll)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the register width of an hll.
--
CREATE FUNCTION hll_regwidth(hll)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the maximum explicit threshold of an hll.
--
CREATE FUNCTION hll_expthresh(hll, OUT specified bigint, OUT effective bigint)
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Returns the sparse enabled value of an hll.
--
CREATE FUNCTION hll_sparseon(hll)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Set output version.
--
CREATE FUNCTION hll_set_output_version(integer)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Set sparse to full compressed threshold to fixed value.
--
CREATE FUNCTION hll_set_max_sparse(integer)
     RETURNS integer
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- Change the default type modifier, empty and add aggregate defaults.
CREATE FUNCTION hll_set_defaults(IN i_log2m integer,
                                 IN i_regwidth integer,
                                 IN i_expthresh bigint,
                                 IN i_sparseon integer,
                                 OUT o_log2m integer,
                                 OUT o_regwidth integer,
                                 OUT o_expthresh bigint,
                                 OUT o_sparseon integer)
     AS 'MODULE_PATHNAME'
     LANGUAGE C STRICT IMMUTABLE;

-- ----------------------------------------------------------------
-- Murmur Hashing
-- ----------------------------------------------------------------

-- Hash a boolean.
--
CREATE FUNCTION hll_hash_boolean(boolean, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_1byte'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash a smallint.
--
CREATE FUNCTION hll_hash_smallint(smallint, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_2byte'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash an integer.
--
CREATE FUNCTION hll_hash_integer(integer, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_4byte'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash a bigint.
--
CREATE FUNCTION hll_hash_bigint(bigint, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_8byte'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash a byte array.
--
CREATE FUNCTION hll_hash_bytea(bytea, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_varlena'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash a text.
--
CREATE FUNCTION hll_hash_text(text, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_varlena'
     LANGUAGE C STRICT IMMUTABLE;

-- Hash any scalar data type.
--
CREATE FUNCTION hll_hash_any(anyelement, integer default 0)
     RETURNS hll_hashval
     AS 'MODULE_PATHNAME', 'hll_hash_any'
     LANGUAGE C STRICT IMMUTABLE;


-- ----------------------------------------------------------------
-- Operators
-- ----------------------------------------------------------------

CREATE OPERATOR = (
	LEFTARG = hll, RIGHTARG = hll, PROCEDURE = hll_eq,
	COMMUTATOR = '=', NEGATOR = '<>',
	RESTRICT = eqsel, JOIN = eqjoinsel,
	MERGES
);

CREATE OPERATOR <> (
	LEFTARG = hll, RIGHTARG = hll, PROCEDURE = hll_ne,
	COMMUTATOR = '<>', NEGATOR = '=',
	RESTRICT = neqsel, JOIN = neqjoinsel
);

CREATE OPERATOR || (
       LEFTARG = hll, RIGHTARG = hll, PROCEDURE = hll_union
);

CREATE OPERATOR || (
       LEFTARG = hll, RIGHTARG = hll_hashval, PROCEDURE = hll_add
);

CREATE OPERATOR || (
       LEFTARG = hll_hashval, RIGHTARG = hll, PROCEDURE = hll_add_rev
);

CREATE OPERATOR # (
       RIGHTARG = hll, PROCEDURE = hll_cardinality
);

-- ----------------------------------------------------------------
-- Aggregates
-- ----------------------------------------------------------------

-- Union aggregate transition function, first arg internal data
-- structure, second arg is a packed multiset.
--
CREATE FUNCTION hll_union_trans(internal, hll)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

-- NOTE - unfortunately aggregate functions don't support default
-- arguments so we need to declare 5 signatures.

-- Add aggregate transition function, first arg internal data
-- structure, second arg is a hashed value.  Remaining args are log2n,
-- regwidth, expthresh, sparseon.
--
CREATE FUNCTION hll_add_trans4(internal,
                               hll_hashval,
                               integer,
                               integer,
                               bigint,
                               integer)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

CREATE FUNCTION hll_add_trans3(internal,
                               hll_hashval,
                               integer,
                               integer,
                               bigint)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

CREATE FUNCTION hll_add_trans2(internal,
                               hll_hashval,
                               integer,
                               integer)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

CREATE FUNCTION hll_add_trans1(internal,
                               hll_hashval,
                               integer)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

CREATE FUNCTION hll_add_trans0(internal,
                               hll_hashval)
     RETURNS internal
     AS 'MODULE_PATHNAME'
     LANGUAGE C;


-- Converts internal data structure into packed multiset.
--
CREATE FUNCTION hll_pack(internal)
     RETURNS hll
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

-- Computes cardinality of internal data structure.
--
CREATE FUNCTION hll_card_unpacked(internal)
     RETURNS double precision
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

-- Computes floor(cardinality) of internal data structure.
--
CREATE FUNCTION hll_floor_card_unpacked(internal)
     RETURNS int8
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

-- Computes ceil(cardinality) of internal data structure.
--
CREATE FUNCTION hll_ceil_card_unpacked(internal)
     RETURNS int8
     AS 'MODULE_PATHNAME'
     LANGUAGE C;

-- Union aggregate function, returns hll.
--
CREATE AGGREGATE hll_union_agg (hll) (
       SFUNC = hll_union_trans,
       STYPE = internal,
       FINALFUNC = hll_pack
);

-- NOTE - unfortunately aggregate functions don't support default
-- arguments so we need to declare 5 signatures.

-- Add aggregate function, returns hll.
CREATE AGGREGATE hll_add_agg (hll_hashval) (
       SFUNC = hll_add_trans0,
       STYPE = internal,
       FINALFUNC = hll_pack
);

-- Add aggregate function, returns hll.
CREATE AGGREGATE hll_add_agg (hll_hashval, integer) (
       SFUNC = hll_add_trans1,
       STYPE = internal,
       FINALFUNC = hll_pack
);

-- Add aggregate function, returns hll.
CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer) (
       SFUNC = hll_add_trans2,
       STYPE = internal,
       FINALFUNC = hll_pack
);

-- Add aggregate function, returns hll.
CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint) (
       SFUNC = hll_add_trans3,
       STYPE = internal,
       FINALFUNC = hll_pack
);

-- Add aggregate function, returns hll.
CREATE AGGREGATE hll_add_agg (hll_hashval, integer, integer, bigint, integer) (
       SFUNC = hll_add_trans4,
       STYPE = internal,
       FINALFUNC = hll_pack
);
