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
