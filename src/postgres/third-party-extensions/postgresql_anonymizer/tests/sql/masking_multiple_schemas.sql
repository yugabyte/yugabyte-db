BEGIN;

CREATE SCHEMA sales;
CREATE SCHEMA "HR";
CREATE SCHEMA marketing;

CREATE TABLE sales.staff(
    staff_id SERIAL PRIMARY KEY,
    firstname VARCHAR(45) NOT NULL,
    lastname VARCHAR(45) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE
);
COMMENT ON COLUMN sales.staff.lastname IS 'MASKED WITH FUNCTION anon.random_last_name()';

INSERT INTO sales.staff
VALUES ( 101, 'Michael' , 'Scott', 'michael.scott@the-office.com');

CREATE TABLE "HR".staff(
    staff_id SERIAL PRIMARY KEY,
    firstname VARCHAR(45) NOT NULL,
    lastname VARCHAR(45) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE
);
COMMENT ON COLUMN "HR".staff.lastname IS 'MASKED WITH FUNCTION anon.random_last_name()';

INSERT INTO "HR".staff
VALUES ( 888, 'Dwight', 'Schrute' , 'dwight.schrute@the-office.com');

CREATE TABLE marketing.staff(
    staff_id SERIAL PRIMARY KEY,
    firstname VARCHAR(45) NOT NULL,
    lastname VARCHAR(45) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE
);
COMMENT ON COLUMN marketing.staff.lastname IS 'MASKED WITH FUNCTION anon.random_last_name()';

INSERT INTO marketing.staff
VALUES ( 294, 'Jim' , 'Halpert', 'jim.halpert@the-office.com');

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

SELECT anon.dump();

SELECT anon.anonymize();

SELECT * FROM sales.staff;

SELECT anon.start_dynamic_masking();

ROLLBACK;
