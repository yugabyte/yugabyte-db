----
-- Add LEAST/GREATEST declaration to return NULL on NULL input.
-- PostgreSQL only returns NULL when all the parameters are NULL.
----

-- GREATEST
CREATE FUNCTION oracle.greatest(integer, integer)
RETURNS integer
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(integer, integer, integer)
RETURNS integer
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(smallint, smallint)
RETURNS smallint
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(smallint, smallint, smallint)
RETURNS smallint
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(numeric, numeric)
RETURNS numeric
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(numeric, numeric, numeric)
RETURNS numeric
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(bigint, bigint)
RETURNS bigint
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(bigint, bigint, bigint)
RETURNS bigint
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(bpchar, bpchar)
RETURNS bpchar
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(bpchar, bpchar, bpchar)
RETURNS bpchar
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(text, text)
RETURNS text
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(text, text, text)
RETURNS text
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(date, date)
RETURNS date
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(date, date, date)
RETURNS date
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(time, time)
RETURNS time
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(time, time, time)
RETURNS time
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(timestamp, timestamp)
RETURNS timestamp
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(timestamp, timestamp, timestamp)
RETURNS timestamp
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(timestamptz, timestamptz)
RETURNS timestamptz
AS
'SELECT greatest($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(timestamptz, timestamptz, timestamptz)
RETURNS timestamptz
AS
'SELECT greatest($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.greatest(anynonarray, VARIADIC anyarray)
RETURNS anynonarray
AS 'MODULE_PATHNAME', 'ora_greatest'
LANGUAGE C IMMUTABLE STRICT;

-- LEAST
CREATE FUNCTION oracle.least(integer, integer)
RETURNS integer
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(integer, integer, integer)
RETURNS integer
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(smallint, smallint)
RETURNS smallint
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(smallint, smallint, smallint)
RETURNS smallint
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(numeric, numeric)
RETURNS numeric
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(numeric, numeric, numeric)
RETURNS numeric
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(bigint, bigint)
RETURNS bigint
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(bigint, bigint, bigint)
RETURNS bigint
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(bpchar, bpchar)
RETURNS bpchar
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(bpchar, bpchar, bpchar)
RETURNS bpchar
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(text, text)
RETURNS text
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(text, text, text)
RETURNS text
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(date, date)
RETURNS date
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(date, date, date)
RETURNS date
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(time, time)
RETURNS time
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(time, time, time)
RETURNS time
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(timestamp, timestamp)
RETURNS timestamp
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(timestamp, timestamp, timestamp)
RETURNS timestamp
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(timestamptz, timestamptz)
RETURNS timestamptz
AS
'SELECT least($1, $2)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(timestamptz, timestamptz, timestamptz)
RETURNS timestamptz
AS
'SELECT least($1, $2, $3)'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION oracle.least(anynonarray, VARIADIC anyarray)
RETURNS anynonarray
AS 'MODULE_PATHNAME', 'ora_least'
LANGUAGE C IMMUTABLE STRICT;
