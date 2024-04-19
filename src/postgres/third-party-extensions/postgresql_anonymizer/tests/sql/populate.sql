BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.init();

TRUNCATE anon.email;

COPY anon.email
  FROM PROGRAM
  '/usr/bin/python3 $(pg_config --sharedir)/extension/anon/populate.py --table email --lines 500';

SELECT count(DISTINCT val)=500 FROM anon.email;

ROLLBACK;
