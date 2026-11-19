BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;

CREATE TABLE public.fake_names
  AS SELECT anon.dummy_name() AS name FROM generate_series(1,1000);

CREATE FUNCTION public.fake_name()
  RETURNS TEXT
AS $$
  SELECT name FROM fake_names ORDER BY random() LIMIT 1;
$$
 LANGUAGE SQL
;

SECURITY LABEL FOR anon ON FUNCTION public.fake_name() IS 'TRUSTED';

CREATE TABLE public.customer (
  id INT,
  name TEXT
);

INSERT INTO public.customer
VALUES
  (1,'Alice'),
  (2,'Bob');

SECURITY LABEL FOR anon ON COLUMN public.customer.name
  IS 'MASKED WITH FUNCTION public.fake_name()';

SELECT anon.anonymize_database();

ROLLBACK;
