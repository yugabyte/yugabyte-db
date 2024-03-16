\set ECHO none
set client_min_messages TO error;
CREATE EXTENSION IF NOT EXISTS orafce;
GRANT ALL ON SCHEMA public TO public;
SET client_min_messages TO default;
SET search_path TO public, oracle;
