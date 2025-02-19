
/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION anon CASCADE;


-- This should RAISE NOTICE
SELECT anon.detect();

-- INIT
BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665


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


/* YB: workaround for lack of transactional DDL
ROLLBACK;
*/ -- YB

DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
DROP TABLE customer CASCADE; -- YB: workaround for lack of transactional DDL 
DROP TABLE vendor CASCADE; -- YB: workaround for lack of transactional DDL 
DROP TABLE vendeur CASCADE; -- YB: workaround for lack of transactional DDL 
