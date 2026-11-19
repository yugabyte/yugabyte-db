create table invoice (
  invoice_ref SERIAL,
  invoice_date DATE,
  invoice_cost FLOAT,
  invoice_recipient TEXT
);

create table invoice_2020 (
  CHECK (
      invoice_date >= DATE '2020-01-01'
  AND invoice_date < DATE '2021-01-01'
  )
)
INHERITS (invoice);

create table invoice_2019 (
  CHECK (
      invoice_date >= DATE '2019-01-01'
  AND invoice_date < DATE '2020-01-01'
  )
)
INHERITS (invoice);

INSERT INTO invoice_2020
VALUES ( 2453, '2020-10-28', 99.99, 'John Smith');

CREATE TABLE measurement (
    city_id         int not null,
    logdate         date not null,
    peaktemp        int,
    unitsales       int,
    operator_name   text
) PARTITION BY RANGE (logdate);

CREATE TABLE measurement_y2006m02 PARTITION OF measurement
    FOR VALUES FROM ('2006-02-01') TO ('2006-03-01');

CREATE TABLE measurement_y2006m03 PARTITION OF measurement
    FOR VALUES FROM ('2006-03-01') TO ('2006-04-01');

INSERT INTO measurement_y2006m03
VALUES ( 2453, '2006-03-28', 30, 0, 'John Smith');

CREATE EXTENSION anon CASCADE;

SELECT anon.init();

SECURITY LABEL FOR anon ON COLUMN invoice.invoice_recipient
  IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

SECURITY LABEL FOR anon ON COLUMN invoice_2020.invoice_recipient
  IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR anon ON COLUMN measurement.operator_name
  IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

SECURITY LABEL FOR anon ON COLUMN measurement_y2006m02.operator_name
  IS 'MASKED WITH VALUE NULL';

CREATE ROLE datascientist LOGIN;
SECURITY LABEL FOR anon ON ROLE datascientist IS 'MASKED';

SELECT anon.start_dynamic_masking();

\c - datascientist

SELECT * FROM invoice;

SELECT * FROM invoice_2020;

SELECT * FROM measurement;

SELECT * FROM measurement_y2006m02;
