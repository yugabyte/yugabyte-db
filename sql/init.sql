\set ECHO none
set client_min_messages TO error;
CREATE EXTENSION IF NOT EXISTS orafce;
GRANT ALL ON SCHEMA public TO public;
set client_min_messages TO default;