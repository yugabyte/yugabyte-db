
BEGIN;

CREATE EXTENSION anon CASCADE;


-- This should RAISE NOTICE
SELECT anon.detect();

-- INIT
SELECT anon.init();


CREATE TABLE customer (
  id SERIAL,
  firstname TEXT,
  last_name TEXT,
  "CreditCard" TEXT
);

CREATE TABLE vendor (
  employee_id INTEGER UNIQUE,
  "Firstname" TEXT,
  lastname TEXT,
  phone_number TEXT,
  birth DATE
);

CREATE TABLE vendeur (
  identifiant INTEGER UNIQUE,
  "Prenom" TEXT,
  nom TEXT,
  telephone TEXT,
  date_naissance DATE
);


SELECT anon.detect() IS NOT NULL LIMIT 1;

SELECT count(*) = 6  FROM anon.detect('fr_FR');

SELECT count(*) = 5  FROM anon.detect('en_US');

SELECT count(*) = 0  FROM anon.detect('fnkgfdlg,sdkf,vkvsld');


ROLLBACK;
