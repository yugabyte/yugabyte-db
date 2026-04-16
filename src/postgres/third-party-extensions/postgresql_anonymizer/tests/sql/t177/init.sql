--
-- https://gitlab.com/dalibo/postgresql_anonymizer/-/issues/177
--

CREATE SCHEMA confidential;

CREATE TABLE public.customer (
  id INT PRIMARY KEY
);

CREATE TABLE confidential.contacts (
  contact_id SERIAL,
  customer_id INT,
  name TEXT,
  phone TEXT,
  email TEXT,
  CONSTRAINT fk_customer_id
    FOREIGN KEY(customer_id)
    REFERENCES public.customer(id)
);
