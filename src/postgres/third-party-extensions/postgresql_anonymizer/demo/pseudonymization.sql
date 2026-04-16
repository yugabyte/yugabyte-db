BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

CREATE TABLE people (
  id   TEXT,
  name TEXT
);

INSERT INTO people
VALUES
('a2302a68-1ea6-41a3-a690-0d9536969753','john'),
('e7084f6e-78b7-4870-b5db-3b9cfb30b9d2','bob'),
('4f6fa879-62ff-4195-8f1d-dba4dea8e7d1','sidney');


-- once
SELECT
  anon.fake_first_name() AS fake_name,
  anon.pseudo_first_name('a2302a68-1ea6-41a3-a690-0d9536969753') AS pseudo_name;

-- twice
SELECT
  anon.fake_first_name() AS fake_name,
  anon.pseudo_first_name('a2302a68-1ea6-41a3-a690-0d9536969753') AS pseudo_name;

-- with salt
SELECT
  anon.pseudo_first_name('john','123salt') AS salt1,
  anon.pseudo_first_name('john','ccke25K302') AS salt2

;

SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.pseudo_first_name(id)';

-- Anonymize once
SELECT anon.anonymize_table('people');
SELECT * FROM people;

-- Anonymize twice
SELECT anon.anonymize_table('people');
SELECT * FROM people;


ROLLBACK;

