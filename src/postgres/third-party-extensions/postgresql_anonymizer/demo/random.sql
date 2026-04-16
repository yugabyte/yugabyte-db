BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

--let's use `TEMPORARY` instead of `pg_temp` for clarity
--SET search_path TO pg_temp, public;

CREATE TEMPORARY TABLE customer(
  id SERIAL,
  full_name TEXT,
  hair_color TEXT,
  birth DATE,
  employer TEXT,
  zipcode TEXT,
  fk_shop INTEGER
);

INSERT INTO customer
VALUES
(911,'Chuck Norris','brown','1940-03-10','Texas Rangers', '75001',12),
(312,'David Hasselhoff','black','1952-07-17','Baywatch', '90001',423)
;

SELECT * FROM customer;

UPDATE customer
SET
  full_name=anon.random_first_name() || ' ' || anon.random_last_name(),
  hair_color=anon.random_in(ARRAY['black','blonde','brown','red',NULL]),
  birth=anon.random_date_between('1920-01-01'::DATE,now()),
  employer=anon.random_company(),
  zipcode=anon.random_zip()
;

SELECT * FROM customer;

ROLLBACK;
