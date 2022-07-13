--
-- MONEY
--
-- Note that we assume lc_monetary has been set to C.
--

SET LC_MONETARY TO 'en_US.UTF-8';

CREATE TABLE money_data (m money);

INSERT INTO money_data VALUES ('123');
SELECT * FROM money_data;
SELECT m + '123' FROM money_data;
SELECT m + '123.45' FROM money_data;
SELECT m - '123.45' FROM money_data;
SELECT m / '2'::money FROM money_data;
SELECT m * 2 FROM money_data;
SELECT 2 * m FROM money_data;
SELECT m / 2 FROM money_data;
SELECT m * 2::int2 FROM money_data;
SELECT 2::int2 * m FROM money_data;
SELECT m / 2::int2 FROM money_data;
SELECT m * 2::int8 FROM money_data;
SELECT 2::int8 * m FROM money_data;
SELECT m / 2::int8 FROM money_data;
SELECT m * 2::float8 FROM money_data;
SELECT 2::float8 * m FROM money_data;
SELECT m / 2::float8 FROM money_data;
SELECT m * 2::float4 FROM money_data;
SELECT 2::float4 * m FROM money_data;
SELECT m / 2::float4 FROM money_data;

-- All true
SELECT m = '$123.00' FROM money_data;
SELECT m != '$124.00' FROM money_data;
SELECT m <= '$123.00' FROM money_data;
SELECT m >= '$123.00' FROM money_data;
SELECT m < '$124.00' FROM money_data;
SELECT m > '$122.00' FROM money_data;

-- All false
SELECT m = '$123.01' FROM money_data;
SELECT m != '$123.00' FROM money_data;
SELECT m <= '$122.99' FROM money_data;
SELECT m >= '$123.01' FROM money_data;
SELECT m > '$124.00' FROM money_data;
SELECT m < '$122.00' FROM money_data;

SELECT cashlarger(m, '$124.00') FROM money_data;
SELECT cashsmaller(m, '$124.00') FROM money_data;
SELECT cash_words(m) FROM money_data;
SELECT cash_words(m + '1.23') FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.45');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.451');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.454');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.455');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.456');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$123.459');
SELECT * FROM money_data;

--
-- Test various formats
--
DELETE FROM money_data;
INSERT INTO money_data VALUES ('0');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('-100');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('2.0001');
SELECT * FROM money_data;
SELECT m + '0.0099' FROM money_data;
SELECT m + '0.0098' FROM money_data;
SELECT m + '0.0050' FROM money_data;
SELECT m + '0.0049' FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('5,.06');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$3.0001');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$40');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('1,2');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('1,23');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('100,120');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('100,23');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('1000,23');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('1,000,000.12');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('2,000.00012');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$3,000.00012');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('$4,000,000.12');
SELECT * FROM money_data;

-- documented minimums and maximums
DELETE FROM money_data;
INSERT INTO money_data VALUES ('-92233720368547758.08');
SELECT * FROM money_data;

DELETE FROM money_data;
INSERT INTO money_data VALUES ('92233720368547758.07');
SELECT * FROM money_data;

-- input checks
SELECT '1234567890'::money;
SELECT '12345678901234567'::money;
-- TODO: Enable after issue #808 is done (ENG-4604)
--       (fails solely on yugabyte-centos-phabricator-gcc-release)
-- SELECT '123456789012345678'::money;
-- ERROR:  value "123456789012345678" is out of range for type money
-- LINE 1: SELECT '123456789012345678'::money;
-- ^
-- SELECT '9223372036854775807'::money;
-- ERROR:  value "9223372036854775807" is out of range for type money
-- LINE 1: SELECT '9223372036854775807'::money;
-- ^
SELECT '-12345'::money;
SELECT '-1234567890'::money;
SELECT '-12345678901234567'::money;
-- TODO: Enable after issue #808 is done (ENG-4604)
-- SELECT '-123456789012345678'::money;
-- ERROR:  value "-123456789012345678" is out of range for type money
-- LINE 1: SELECT '-123456789012345678'::money;
--                ^
-- SELECT '-9223372036854775808'::money;
-- ERROR:  value "-9223372036854775808" is out of range for type money
-- LINE 1: SELECT '-9223372036854775808'::money;
--                ^

-- special characters
SELECT '(1)'::money;
SELECT '($123,456.78)'::money;

-- documented minimums and maximums
SELECT '-92233720368547758.08'::money;
SELECT '92233720368547758.07'::money;

-- TODO: Enable after issue #808 is done (ENG-4604)
-- SELECT '-92233720368547758.09'::money;
-- ERROR:  value "-92233720368547758.09" is out of range for type money
-- LINE 1: SELECT '-92233720368547758.09'::money;
--                ^
-- SELECT '92233720368547758.08'::money;
-- ERROR:  value "92233720368547758.08" is out of range for type money
-- LINE 1: SELECT '92233720368547758.08'::money;
--                ^

-- rounding
SELECT '-92233720368547758.085'::money;
-- TODO: Enable after issue #808 is done (ENG-4604)
-- SELECT '92233720368547758.075'::money;
-- ERROR:  value "92233720368547758.075" is out of range for type money
-- LINE 1: SELECT '92233720368547758.075'::money;
--                ^

-- rounding vs. truncation in division
SELECT '878.08'::money / 11::float8;
SELECT '878.08'::money / 11::float4;
SELECT '878.08'::money / 11::bigint;
SELECT '878.08'::money / 11::int;
SELECT '878.08'::money / 11::smallint;

-- check for precision loss in division
SELECT '90000000000000099.00'::money / 10::bigint;
SELECT '90000000000000099.00'::money / 10::int;
SELECT '90000000000000099.00'::money / 10::smallint;

-- Cast int4/int8/numeric to money
SELECT 1234567890::money;
SELECT 12345678901234567::money;
SELECT (-12345)::money;
SELECT (-1234567890)::money;
SELECT (-12345678901234567)::money;
SELECT 1234567890::int4::money;
SELECT 12345678901234567::int8::money;
SELECT 12345678901234567::numeric::money;
SELECT (-1234567890)::int4::money;
SELECT (-12345678901234567)::int8::money;
SELECT (-12345678901234567)::numeric::money;

-- Cast from money
SELECT '12345678901234567'::money::numeric;
SELECT '-12345678901234567'::money::numeric;
SELECT '92233720368547758.07'::money::numeric;
SELECT '-92233720368547758.08'::money::numeric;

--
-- Test for PRIMARY KEY
--

CREATE TABLE money_data_with_pk(id MONEY PRIMARY KEY, val money);
INSERT INTO money_data_with_pk VALUES ('1.1','-11.11');
INSERT INTO money_data_with_pk VALUES ('2.2','-22.22');
INSERT INTO money_data_with_pk VALUES ('3.3','-33.33');
SELECT * FROM money_data_with_pk ORDER BY id;
SELECT VAL FROM money_data_with_pk WHERE id = '$2.2';

-- ASC/DESC check
SELECT * FROM money_data_with_pk ORDER BY val ASC;
SELECT * FROM money_data_with_pk ORDER BY val DESC;

CREATE TEMP TABLE IF NOT EXISTS t0(c0 money );
INSERT INTO t0(c0) VALUES((0.3528332)::MONEY);
UPDATE t0 SET c0 = (0.7406399)::MONEY WHERE (((0.023219043)::MONEY) BETWEEN (CAST(0.19029781 AS MONEY)) AND (CAST(0.0984419 AS MONEY))) IS FALSE;
