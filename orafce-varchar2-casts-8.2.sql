/* CREATE CAST */
CREATE CAST (nvarchar2 AS text)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (text AS nvarchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (nvarchar2 AS char)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (char AS nvarchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (nvarchar2 AS varchar)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (varchar AS nvarchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (nvarchar2 AS nvarchar2)
WITH FUNCTION nvarchar2(nvarchar2,integer, boolean)
AS IMPLICIT;

CREATE OR REPLACE FUNCTION nvarchar2_real(nvarchar2) RETURNS real
AS $$ SELECT $1::text::real$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION real_nvarchar2(real) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_double(nvarchar2) RETURNS double precision
AS $$ SELECT $1::text::double precision$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION double_nvarchar2(double precision) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_integer(nvarchar2) RETURNS integer
AS $$ SELECT $1::text::integer$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION integer_nvarchar2(integer) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_smallint(nvarchar2) RETURNS smallint
AS $$ SELECT $1::text::smallint$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION smallint_nvarchar2(smallint) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_bigint(nvarchar2) RETURNS bigint
AS $$ SELECT $1::text::bigint$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION bigint_nvarchar2(bigint) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_numeric(nvarchar2) RETURNS numeric
AS $$ SELECT $1::text::numeric$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION numeric_nvarchar2(numeric) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_date(nvarchar2) RETURNS date
AS $$ SELECT $1::text::date$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION date_nvarchar2(date) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_timestamp(nvarchar2) RETURNS timestamp
AS $$ SELECT $1::text::timestamp$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION timestamp_nvarchar2(timestamp) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION nvarchar2_interval(nvarchar2) RETURNS interval
AS $$ SELECT $1::text::interval$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION interval_nvarchar2(interval) RETURNS nvarchar2
AS $$ SELECT $1::text::nvarchar2$$ LANGUAGE sql;

CREATE CAST (nvarchar2 AS real)
WITH FUNCTION nvarchar2_real(nvarchar2)
AS IMPLICIT;

CREATE CAST (real AS nvarchar2)
WITH FUNCTION real_nvarchar2(real)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS double precision)
WITH FUNCTION nvarchar2_double(nvarchar2)
AS IMPLICIT;

CREATE CAST (double precision AS nvarchar2)
WITH FUNCTION double_nvarchar2(double precision)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS integer)
WITH FUNCTION nvarchar2_integer(nvarchar2)
AS IMPLICIT;

CREATE CAST (integer AS nvarchar2)
WITH FUNCTION integer_nvarchar2(integer)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS smallint)
WITH FUNCTION nvarchar2_smallint(nvarchar2)
AS IMPLICIT;

CREATE CAST (smallint AS nvarchar2)
WITH FUNCTION smallint_nvarchar2(smallint)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS bigint)
WITH FUNCTION nvarchar2_bigint(nvarchar2)
AS IMPLICIT;

CREATE CAST (bigint AS nvarchar2)
WITH FUNCTION bigint_nvarchar2(bigint)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS numeric)
WITH FUNCTION nvarchar2_numeric(nvarchar2)
AS IMPLICIT;

CREATE CAST (numeric AS nvarchar2)
WITH FUNCTION numeric_nvarchar2(numeric)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS date)
WITH FUNCTION nvarchar2_date(nvarchar2)
AS IMPLICIT;

CREATE CAST (date AS nvarchar2)
WITH FUNCTION date_nvarchar2(date)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS timestamp)
WITH FUNCTION nvarchar2_timestamp(nvarchar2)
AS IMPLICIT;

CREATE CAST (timestamp AS nvarchar2)
WITH FUNCTION timestamp_nvarchar2(timestamp)
AS IMPLICIT;

CREATE CAST (nvarchar2 AS interval)
WITH FUNCTION nvarchar2_interval(nvarchar2)
AS IMPLICIT;

CREATE CAST (interval AS nvarchar2)
WITH FUNCTION interval_nvarchar2(interval)
AS IMPLICIT;

/* CREATE CAST */
CREATE CAST (varchar2 AS text)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (text AS varchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (varchar2 AS char)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (char AS varchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (varchar2 AS varchar)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (varchar AS varchar2)
WITHOUT FUNCTION
AS IMPLICIT;

CREATE CAST (varchar2 AS varchar2)
WITH FUNCTION varchar2(varchar2,integer,boolean)
AS IMPLICIT;

CREATE OR REPLACE FUNCTION varchar2_real(varchar2) RETURNS real
AS $$ SELECT $1::text::real$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION real_varchar2(real) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_double(varchar2) RETURNS double precision
AS $$ SELECT $1::text::double precision$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION double_varchar2(double precision) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_integer(varchar2) RETURNS integer
AS $$ SELECT $1::text::integer$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION integer_varchar2(integer) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_smallint(varchar2) RETURNS smallint
AS $$ SELECT $1::text::smallint$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION smallint_varchar2(smallint) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_bigint(varchar2) RETURNS bigint
AS $$ SELECT $1::text::bigint$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION bigint_varchar2(bigint) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_numeric(varchar2) RETURNS numeric
AS $$ SELECT $1::text::numeric$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION numeric_varchar2(numeric) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_date(varchar2) RETURNS date
AS $$ SELECT $1::text::date$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION date_varchar2(date) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_timestamp(varchar2) RETURNS timestamp
AS $$ SELECT $1::text::timestamp$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION timestamp_varchar2(timestamp) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION varchar2_interval(varchar2) RETURNS interval
AS $$ SELECT $1::text::interval$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION interval_varchar2(interval) RETURNS varchar2
AS $$ SELECT $1::text::varchar2$$ LANGUAGE sql;

CREATE CAST (varchar2 AS real)
WITH FUNCTION varchar2_real(varchar2)
AS IMPLICIT;

CREATE CAST (real AS varchar2)
WITH FUNCTION real_varchar2(real)
AS IMPLICIT;

CREATE CAST (varchar2 AS double precision)
WITH FUNCTION varchar2_double(varchar2)
AS IMPLICIT;

CREATE CAST (double precision AS varchar2)
WITH FUNCTION double_varchar2(double precision)
AS IMPLICIT;

CREATE CAST (varchar2 AS integer)
WITH FUNCTION varchar2_integer(varchar2)
AS IMPLICIT;

CREATE CAST (integer AS varchar2)
WITH FUNCTION integer_varchar2(integer)
AS IMPLICIT;

CREATE CAST (varchar2 AS smallint)
WITH FUNCTION varchar2_smallint(varchar2)
AS IMPLICIT;

CREATE CAST (smallint AS varchar2)
WITH FUNCTION smallint_varchar2(smallint)
AS IMPLICIT;

CREATE CAST (varchar2 AS bigint)
WITH FUNCTION varchar2_bigint(varchar2)
AS IMPLICIT;

CREATE CAST (bigint AS varchar2)
WITH FUNCTION bigint_varchar2(bigint)
AS IMPLICIT;

CREATE CAST (varchar2 AS numeric)
WITH FUNCTION varchar2_numeric(varchar2)
AS IMPLICIT;

CREATE CAST (numeric AS varchar2)
WITH FUNCTION numeric_varchar2(numeric)
AS IMPLICIT;

CREATE CAST (varchar2 AS date)
WITH FUNCTION varchar2_date(varchar2)
AS IMPLICIT;

CREATE CAST (date AS varchar2)
WITH FUNCTION date_varchar2(date)
AS IMPLICIT;

CREATE CAST (varchar2 AS timestamp)
WITH FUNCTION varchar2_timestamp(varchar2)
AS IMPLICIT;

CREATE CAST (timestamp AS varchar2)
WITH FUNCTION timestamp_varchar2(timestamp)
AS IMPLICIT;

CREATE CAST (varchar2 AS interval)
WITH FUNCTION varchar2_interval(varchar2)
AS IMPLICIT;

CREATE CAST (interval AS varchar2)
WITH FUNCTION interval_varchar2(interval)
AS IMPLICIT;

--
-- Operator Functions.
--

CREATE OR REPLACE FUNCTION nvarchar2_eq( nvarchar2, nvarchar2 )
RETURNS bool
AS 'texteq'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_ne( nvarchar2, nvarchar2 )
RETURNS bool
AS 'textne'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_lt( nvarchar2, nvarchar2 )
RETURNS bool
AS 'text_lt'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_le( nvarchar2, nvarchar2 )
RETURNS bool
AS 'text_le'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_gt( nvarchar2, nvarchar2 )
RETURNS bool
AS 'text_gt'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_ge( nvarchar2, nvarchar2 )
RETURNS bool
AS 'text_ge'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION textcat(nvarchar2, nvarchar2)
RETURNS nvarchar2
AS 'textcat'
LANGUAGE internal IMMUTABLE STRICT;

--
-- Operators.
--

CREATE OPERATOR = (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    COMMUTATOR = =,
    NEGATOR    = <>,
    PROCEDURE  = nvarchar2_eq,
    RESTRICT   = eqsel,
    JOIN       = eqjoinsel,
    HASHES,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    NEGATOR    = =,
    COMMUTATOR = <>,
    PROCEDURE  = nvarchar2_ne,
    RESTRICT   = neqsel,
    JOIN       = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    NEGATOR    = >=,
    COMMUTATOR = >,
    PROCEDURE  = nvarchar2_lt,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    NEGATOR    = >,
    COMMUTATOR = >=,
    PROCEDURE  = nvarchar2_le,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR >= (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    NEGATOR    = <,
    COMMUTATOR = <=,
    PROCEDURE  = nvarchar2_ge,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR > (
    LEFTARG    = nvarchar2,
    RIGHTARG   = nvarchar2,
    NEGATOR    = <=,
    COMMUTATOR = <,
    PROCEDURE  = nvarchar2_gt,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR || (
    LEFTARG   = nvarchar2,
    RIGHTARG  = nvarchar2,
    PROCEDURE = textcat
);

--
-- Support functions for indexing.
--

CREATE OR REPLACE FUNCTION nvarchar2_cmp(nvarchar2, nvarchar2)
RETURNS int4
AS 'bttextcmp'
LANGUAGE INTERNAL STRICT IMMUTABLE;

--
-- The btree indexing operator class.
--

CREATE OPERATOR CLASS nvarchar2_ops
DEFAULT FOR TYPE nvarchar2 USING btree AS
    OPERATOR    1   <  (nvarchar2, nvarchar2),
    OPERATOR    2   <= (nvarchar2, nvarchar2),
    OPERATOR    3   =  (nvarchar2, nvarchar2),
    OPERATOR    4   >= (nvarchar2, nvarchar2),
    OPERATOR    5   >  (nvarchar2, nvarchar2),
    FUNCTION    1   nvarchar2_cmp(nvarchar2, nvarchar2);

--
-- Aggregates.
--

CREATE OR REPLACE FUNCTION nvarchar2_smaller(nvarchar2, nvarchar2)
RETURNS nvarchar2
AS 'text_smaller'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION nvarchar2_larger(nvarchar2, nvarchar2)
RETURNS nvarchar2
AS 'text_larger'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE AGGREGATE min(nvarchar2)  (
    SFUNC = nvarchar2_smaller,
    STYPE = nvarchar2,
    SORTOP = <
);

CREATE AGGREGATE max(nvarchar2)  (
    SFUNC = nvarchar2_larger,
    STYPE = nvarchar2,
    SORTOP = >
);


CREATE OR REPLACE FUNCTION lower(nvarchar2)
RETURNS nvarchar2 AS 'lower'
LANGUAGE internal IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION upper(nvarchar2)
RETURNS nvarchar2 AS 'upper'
LANGUAGE internal IMMUTABLE STRICT;

-- needed to avoid "function is not unique" errors
-- XXX find a better way to deal with this...
CREATE FUNCTION quote_literal(nvarchar2)
RETURNS text AS 'quote_literal'
LANGUAGE internal IMMUTABLE STRICT;



--
-- Operator Functions.
--

CREATE OR REPLACE FUNCTION varchar2_eq( varchar2, varchar2 )
RETURNS bool
AS 'texteq'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_ne( varchar2, varchar2 )
RETURNS bool
AS 'textne'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_lt( varchar2, varchar2 )
RETURNS bool
AS 'text_lt'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_le( varchar2, varchar2 )
RETURNS bool
AS 'text_le'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_gt( varchar2, varchar2 )
RETURNS bool
AS 'text_gt'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_ge( varchar2, varchar2 )
RETURNS bool
AS 'text_ge'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION textcat(varchar2, varchar2)
RETURNS varchar2
AS 'textcat'
LANGUAGE internal IMMUTABLE STRICT;

--
-- Operators.
--

CREATE OPERATOR = (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    COMMUTATOR = =,
    NEGATOR    = <>,
    PROCEDURE  = varchar2_eq,
    RESTRICT   = eqsel,
    JOIN       = eqjoinsel,
    HASHES,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    NEGATOR    = =,
    COMMUTATOR = <>,
    PROCEDURE  = varchar2_ne,
    RESTRICT   = neqsel,
    JOIN       = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    NEGATOR    = >=,
    COMMUTATOR = >,
    PROCEDURE  = varchar2_lt,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    NEGATOR    = >,
    COMMUTATOR = >=,
    PROCEDURE  = varchar2_le,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR >= (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    NEGATOR    = <,
    COMMUTATOR = <=,
    PROCEDURE  = varchar2_ge,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR > (
    LEFTARG    = varchar2,
    RIGHTARG   = varchar2,
    NEGATOR    = <=,
    COMMUTATOR = <,
    PROCEDURE  = varchar2_gt,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR || (
    LEFTARG   = varchar2,
    RIGHTARG  = varchar2,
    PROCEDURE = textcat
);

--
-- Support functions for indexing.
--

CREATE OR REPLACE FUNCTION varchar2_cmp(varchar2, varchar2)
RETURNS int4
AS 'bttextcmp'
LANGUAGE INTERNAL STRICT IMMUTABLE;

--
-- The btree indexing operator class.
--

CREATE OPERATOR CLASS varchar2_ops
DEFAULT FOR TYPE varchar2 USING btree AS
    OPERATOR    1   <  (varchar2, varchar2),
    OPERATOR    2   <= (varchar2, varchar2),
    OPERATOR    3   =  (varchar2, varchar2),
    OPERATOR    4   >= (varchar2, varchar2),
    OPERATOR    5   >  (varchar2, varchar2),
    FUNCTION    1   varchar2_cmp(varchar2, varchar2);

--
-- Aggregates.
--

CREATE OR REPLACE FUNCTION varchar2_smaller(varchar2, varchar2)
RETURNS varchar2
AS 'text_smaller'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION varchar2_larger(varchar2, varchar2)
RETURNS varchar2
AS 'text_larger'
LANGUAGE INTERNAL IMMUTABLE STRICT;

CREATE AGGREGATE min(varchar2)  (
    SFUNC = varchar2_smaller,
    STYPE = varchar2,
    SORTOP = <
);

CREATE AGGREGATE max(varchar2)  (
    SFUNC = varchar2_larger,
    STYPE = varchar2,
    SORTOP = >
);


CREATE OR REPLACE FUNCTION lower(varchar2)
RETURNS varchar2 AS 'lower'
LANGUAGE internal IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION upper(varchar2)
RETURNS varchar2 AS 'upper'
LANGUAGE internal IMMUTABLE STRICT;

-- needed to avoid "function is not unique" errors
-- XXX find a better way to deal with this...
CREATE FUNCTION quote_literal(varchar2)
RETURNS text AS 'quote_literal'
LANGUAGE internal IMMUTABLE STRICT;
