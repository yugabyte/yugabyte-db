--
-- This example shows why the shuffling method is usefull with foreign keys
--

BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE city (
  id  SERIAL,	
  name TEXT PRIMARY KEY
);

INSERT INTO city
VALUES 
(1,'Paris'), 
(2,'London'), 
(3,'Sidney');

CREATE TABLE weather (
  id SERIAL,
  fk_city_name TEXT references city(name),
  date_measurement  DATE,
  temperature INT,
  precipitation REAL	
);


INSERT INTO weather 
VALUES
( 1, 'Paris', '2019-01-17', 6, 0.0 ),
( 2, 'Paris', '2019-01-18', 2, 3.0 ),
( 3, 'Paris', '2019-01-19', 0, 4.4 ),
( 4, 'Paris', '2019-01-20', 5, 0.0 ),
( 11, 'London', '2019-01-17', 2, 7.0 ),
( 12, 'London', '2019-01-18', 0, 2.3 ),
( 13, 'London', '2019-01-19', 1, 2.4 ),
( 14, 'London', '2019-01-20', 1, 8.8 ),
( 21, 'Sidney', '2019-01-17', 20, 0.0 ),
( 22, 'Sidney', '2019-01-18', 25, 0.0 ),
( 23, 'Sidney', '2019-01-19', 20, 0.0 ),
( 24, 'Sidney', '2019-01-20', 23, 0.0 )
;

SELECT * FROM weather ORDER BY id;

SELECT anon.shuffle_column('weather','fk_city_name','id');

SELECT * FROM weather ORDER BY id;

-- Clean up
ROLLBACK;

