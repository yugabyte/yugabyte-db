--
-- MONEY
--

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

--
-- SUM(money) overflow.  The aggregate must not be pushed down to DocDB,
-- because DocDB accumulates with plain 64-bit addition and would silently
-- wrap around.  Postgres' cash_pl transition function detects the overflow
-- and raises "money out of range".
--
CREATE TABLE money_sum_overflow(id int PRIMARY KEY, amt money);
INSERT INTO money_sum_overflow VALUES (1, '90000000000000000.00'), (2, '90000000000000000.00'), (3, '1.00');
-- The aggregate must be evaluated in Postgres, not pushed down to DocDB (no
-- "Partial Aggregate" node), so that cash_pl can detect the overflow.
EXPLAIN (VERBOSE, COSTS OFF) SELECT sum(amt) FROM money_sum_overflow;
-- min/max/count over money remain pushed down.
EXPLAIN (VERBOSE, COSTS OFF) SELECT min(amt), max(amt), count(amt) FROM money_sum_overflow;
-- The sum overflows int64 cents; it must raise an error, not a negative value.
SELECT sum(amt) FROM money_sum_overflow;
-- A sum that fits still works.
DELETE FROM money_sum_overflow WHERE id = 2;
SELECT sum(amt) = '90000000000000001.00'::money AS ok FROM money_sum_overflow;
DROP TABLE money_sum_overflow;
