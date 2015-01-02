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
