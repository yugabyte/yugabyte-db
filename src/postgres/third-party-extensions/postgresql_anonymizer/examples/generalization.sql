--
-- G E N E R A L I Z A T I O N
--
--

BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;

CREATE TABLE patient (
  ssn TEXT,
  firstname TEXT,
  zipcode INTEGER,
  birth DATE,
  disease TEXT
);

INSERT INTO patient
VALUES
  ('253-51-6170','Alice',47012,'1989-12-29','Heart Disease'),
  ('091-20-0543','Bob',42678,'1979-03-22','Allergy'),
  ('565-94-1926','Caroline',42678,'1971-07-22','Heart Disease'),
  ('510-56-7882','Eleanor',47909,'1989-12-15','Acne'),
  ('098-24-5548','David',47905,'1997-03-04','Flu'),
  ('118-49-5228','Jean',47511,'1993-09-14','Flu'),
  ('263-50-7396','Tim',47900,'1981-02-25','Heart Disease'),
  ('109-99-6362','Bernard',47168,'1992-01-03','Asthma'),
  ('287-17-2794','Sophie',42020,'1972-07-14','Asthma'),
  ('409-28-2014','Arnold',47000,'1999-11-20','Diabetes')
;

SELECT * FROM patient;

CREATE MATERIALIZED VIEW generalized_patient AS
SELECT
    'REDACTED' AS firstname,
    anon.generalize_int4range(zipcode,1000) AS zipcode,
    anon.generalize_daterange(birth,'decade') AS birth,
    disease
FROM patient;


SELECT * FROM generalized_patient;

-- Declare the remaining indirect identifiers of the generalized view
SECURITY LABEL FOR k_anonymity ON COLUMN generalized_patient.zipcode
IS 'INDIRECT IDENTIFIER';

SECURITY LABEL FOR k_anonymity ON COLUMN generalized_patient.birth
IS 'INDIRECT IDENTIFIER';

-- the k factor
SELECT anon.k_anonymity('generalized_patient');

ROLLBACK;
