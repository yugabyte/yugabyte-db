BEGIN;

CREATE TABLE public.invoice (
    name            text,
    amount          float,
    published_date  date
);

CREATE TABLE public.invoice_2020 (
  CHECK ( published_date >= DATE '2020-01-01'
      AND published_date <= DATE '2020-12-31' )
) INHERITS (invoice);

INSERT INTO invoice_2020
VALUES('John DOE', 236.25, '2020-12-09');

CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.init();

--
-- Put mask on the mother table
--
SECURITY LABEL FOR anon ON COLUMN public.invoice.name
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$ ';

SELECT anon.anonymize_table('public.invoice_2020');

SELECT public.invoice.name, public.invoice_2020.name
  FROM public.invoice, public.invoice_2020;

--
-- Put mask on the child table
--
SECURITY LABEL FOR anon ON COLUMN public.invoice_2020.name
IS 'MASKED WITH VALUE $$DELETED$$ ';

SELECT anon.anonymize_table('public.invoice_2020');

SELECT public.invoice.name, public.invoice_2020.name
  FROM public.invoice, public.invoice_2020;



ROLLBACK;
