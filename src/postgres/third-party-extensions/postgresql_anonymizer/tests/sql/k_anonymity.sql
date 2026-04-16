
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE disease (
  id SERIAL,
  name TEXT,
  treatment TEXT
);

SELECT anon.k_anonymity('disease');

CREATE TABLE patient (
  ssn SERIAL,
  firstname TEXT,
  zipcode INTEGER,
  birth DATE,
  disease TEXT
);

COPY patient
FROM STDIN CSV QUOTE AS '"' DELIMITER ' ';
1 "Alice" 47678 "1979-12-29" "Heart Disease"
2 "Bob" 47678 "1979-03-22" "Heart Disease"
3 "Caroline" 47678 "1988-07-22" "Heart Disease"
4 "David" 47905 "1997-03-04" "Flu"
5 "Eleanor" 47909 "1989-12-15" "Heart Disease"
6 "Frank" 47906 "1998-07-04" "Cancer"
7 "Geri" 47605 "1987-10-30" "Heart Disease"
8 "Harry" 47673 "1978-06-13" "Cancer"
9 "Ingrid" 47607 "1991-12-12" "Cancer"
\.

SELECT * FROM patient;

SECURITY LABEL FOR k_anonymity ON COLUMN patient.firstname IS 'INDIRECT IDENTIFIER';
SECURITY LABEL FOR k_anonymity ON COLUMN patient.zipcode IS 'INDIRECT IDENTIFIER';
SECURITY LABEL FOR k_anonymity ON COLUMN patient.birth IS 'INDIRECT IDENTIFIER';

SELECT anon.k_anonymity('patient') = min(kanonymity)
FROM (
  SELECT COUNT(*) as kanonymity
  FROM patient
  GROUP BY firstname, zipcode, birth
) AS k
;


CREATE TEMPORARY TABLE anon_patient
AS SELECT
  'REDACTED'::TEXT AS firstname,
  anon.generalize_int4range(zipcode,1000) AS zipcode,
  anon.generalize_daterange(birth,'decade') AS birth,
  disease
FROM patient
;

SECURITY LABEL FOR k_anonymity ON COLUMN anon_patient.firstname IS 'INDIRECT IDENTIFIER';
SECURITY LABEL FOR k_anonymity ON COLUMN anon_patient.zipcode IS 'INDIRECT IDENTIFIER';
SECURITY LABEL FOR k_anonymity ON COLUMN anon_patient.birth IS 'INDIRECT IDENTIFIER';

SELECT * FROM anon_patient;

SELECT anon.k_anonymity('anon_patient') = min(c)
FROM (
  SELECT COUNT(*) as c
  FROM anon_patient
  GROUP BY firstname, zipcode, birth
) AS k
;



ROLLBACK;
