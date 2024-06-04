BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

--
-- Testing noise() functions
--
SELECT setseed(0.42);
SELECT anon.noise(100::BIGINT,0.5) >= 50 ;
SELECT anon.noise(100::INT,0.33) <= 133;
SELECT anon.noise(0.100,0.1) <= 0.110;
SELECT anon.dnoise('2000-01-01'::DATE,'1 year'::INTERVAL) <= '2001-01-02';
SELECT anon.dnoise('2000-01-01'::TIMESTAMP WITH TIME ZONE,'1 month') <= '2000-02-02';
SELECT anon.dnoise('2000-01-01'::TIMESTAMP WITHOUT TIME ZONE,'1 day') <= '2000-02-02';
SELECT anon.dnoise('09:30:00'::TIME,'1 hour') >= '08:30:00';

-- Noise functions should not fail on min/max values
SELECT MAX(anon.noise(2147483647,0.27)) <= 2147483647
FROM generate_series(1,10);

SELECT MIN(anon.noise(-2147483648,0.27)) >= -2147483648
FROM generate_series(1,10);

SELECT MIN(anon.dnoise('4714-11-24 BC'::DATE,'1 year'::INTERVAL)) >= '4714-11-24 BC'
FROM generate_series(1,10);

SELECT MAX(anon.dnoise('294276-01-01'::DATE,'1 year'::INTERVAL)) >= '294276-01-01'
FROM generate_series(1,10);

--
-- Noise reduction Attack
--
SELECT ROUND(AVG(anon.noise(42,0.33))) = 42
FROM generate_series(1,100000);

--
-- Testing add_noise_on_xxx_column()
--
CREATE TABLE test_noise (
  id SERIAL,
  key   TEXT,
  int_value  INT,
  float_value FLOAT,
  date_value DATE
);

INSERT INTO test_noise
VALUES
( 1, 'a', 40 , 33.3, '2019-01-01'),
( 2, 'b', 40 , 33.3, '2019-01-01'),
( 3, 'c', 40 , 33.3, '2019-01-01'),
( 4, 'd', 40 , 33.3, '2019-01-01'),
( 5, 'e', 40 , 33.3, '2019-01-01'),
( 6, 'f', 40 , 33.3, '2019-01-01'),
( 7, 'g', 40 , 33.3, '2019-01-01'),
( 8, 'h', 40 , 33.3, '2019-01-01'),
( 9, 'i', 40 , 33.3, '2019-01-01'),
( 10, 'j', 40 , 33.3, '2019-01-01')
;

--CREATE TABLE test_noise_backup
--AS SELECT * FROM test_noise;

SELECT anon.add_noise_on_numeric_column('test_noise','int_value', 0.25);

SELECT anon.add_noise_on_numeric_column('test_noise','float_value', 0.5);

SELECT anon.add_noise_on_datetime_column('test_noise','date_value', '365 days');



-- TEST 1 :  int_value is between 30 and 50

SELECT min(int_value) >= 30
AND    max(int_value) <= 50
FROM test_noise;


-- TEST 2 :  float_value is between
SELECT min(float_value) >= 16.6
AND    max(float_value) <= 50
FROM test_noise;

-- TEST 3 :  date_value is between 2018 and 2020
SELECT min(date_value) >= '2018-01-01'
AND    max(date_value) <= '2020-01-01'
FROM test_noise;


DROP EXTENSION anon CASCADE;

DROP TABLE test_noise;

ROLLBACK;
