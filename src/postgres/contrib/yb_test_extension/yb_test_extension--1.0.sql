/* contrib/yb_test_extension/yb_test_extension--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use '''CREATE EXTENSION yb_test_extension''' to load this file. \quit

-- Create all kinds of database objects supported by YB which are not included
-- in existing supported extensions to improve test coverage.
-- Unsupported database objects are excluded from this test extension, namely,
-- OPERATOR CLASS, OPERATOR FAMILY, TRANSFORM, ACCESS METHOD, CONVERSION.

-- Trigger
CREATE TABLE tbl (k INT, v INT);

CREATE OR REPLACE FUNCTION increment_v()
RETURNS TRIGGER
AS
$$
BEGIN
    NEW.v = NEW.v + 1;
    RETURN NEW;
END;
$$
LANGUAGE plpgsql;

CREATE TRIGGER increment_v BEFORE INSERT ON tbl
FOR EACH ROW EXECUTE PROCEDURE increment_v();

-- Event Trigger
CREATE TABLE command_tag (k serial, tag text);

CREATE OR REPLACE FUNCTION record_drop_command()
RETURNS EVENT_TRIGGER
AS
$$
BEGIN
    INSERT INTO command_tag(tag) VALUES (TG_TAG);
END;
$$
LANGUAGE plpgsql;

CREATE EVENT TRIGGER record_drop_command ON sql_drop EXECUTE PROCEDURE record_drop_command();

-- Sequence
CREATE SEQUENCE test_sequence START 101;

-- Privilege
GRANT ALL ON test_sequence TO yb_fdw;

-- TS PARSER
-- Create a text search parser same as the parser: default
CREATE TEXT SEARCH PARSER test_parser (
    START    = prsd_start,
    GETTOKEN = prsd_nexttoken,
    END      = prsd_end,
    HEADLINE = prsd_headline,
    LEXTYPES = prsd_lextype
);

-- TS DICTIONARY
CREATE TEXT SEARCH DICTIONARY test_dict (
    TEMPLATE = simple,
    STOPWORDS = english
);

-- TS TEMPLATE
-- Create a text search template same as the template: simple
CREATE TEXT SEARCH TEMPLATE test_template (
    INIT = dsimple_init,
    LEXIZE = dsimple_lexize
);

-- Test Collation
CREATE COLLATION test_collation FROM "en-x-icu";

-- Test ENUM Type
CREATE TYPE test_enum AS ENUM('c', 'b', 'a');

-- Test Range Type
-- Create a range type same as int4range
CREATE TYPE test_range AS RANGE (
    SUBTYPE = int4,
    SUBTYPE_OPCLASS = int4_ops,
    SUBTYPE_DIFF = int4range_subdiff
);

-- Test Composite Type
CREATE TYPE test_composite AS (col1 text, col2 text);

-- Add a reference to target schema to test schema names (CVE-2023-39417)
SELECT 1 AS @extschema@;
