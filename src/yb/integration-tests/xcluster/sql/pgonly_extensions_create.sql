-- Fail the script on the first error
\set ON_ERROR_STOP on

-- Preinstalled extensions that don't need to be created explicitly
-- CREATE EXTENSION plpgsql;
-- CREATE EXTENSION pg_stat_statements;

CREATE EXTENSION file_fdw;

CREATE EXTENSION fuzzystrmatch;

CREATE EXTENSION pgcrypto;

CREATE EXTENSION postgres_fdw;

CREATE EXTENSION sslinfo;

CREATE EXTENSION "uuid-ossp";

CREATE EXTENSION hypopg;

CREATE EXTENSION pg_stat_monitor;

CREATE EXTENSION pgaudit;

-- Extensions that create new types

CREATE EXTENSION hll;

CREATE EXTENSION hstore;

CREATE EXTENSION pg_trgm;

CREATE EXTENSION pgtap;

CREATE EXTENSION tablefunc;

CREATE EXTENSION vector;

-- Extensions that create tables

-- CREATE EXTENSION orafce;

-- CREATE EXTENSION pg_cron;

-- CREATE EXTENSION pg_hint_plan;

-- CREATE EXTENSION pg_partman;


