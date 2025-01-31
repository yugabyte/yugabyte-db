--
-- YB_FEATURE Testsuite
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
--
-- Numeric Types
CREATE TABLE feature_tab_smallint (feature_col SMALLINT);
CREATE TABLE feature_tab_integer (feature_col INTEGER);
CREATE TABLE feature_tab_bigint (feature_col BIGINT);
CREATE TABLE feature_tab_real (feature_col REAL);
CREATE TABLE feature_tab_double_precision (feature_col DOUBLE PRECISION);
CREATE TABLE feature_tab_decimal (feature_col DECIMAL);
CREATE TABLE feature_tab_numeric (feature_col NUMERIC);
CREATE TABLE feature_tab_smallserial (feature_col SMALLSERIAL);
CREATE TABLE feature_tab_serial (feature_col SERIAL);
CREATE TABLE feature_tab_bigserial (feature_col BIGSERIAL);
--
-- Monetary Types
CREATE TABLE feature_tab_money (feature_col MONEY);
--
-- Character Types
CREATE TABLE feature_tab_character_varying (feature_col CHARACTER VARYING(10));
CREATE TABLE feature_tab_varchar (feature_col VARCHAR(10));
CREATE TABLE feature_tab_character (feature_col CHARACTER(10));
CREATE TABLE feature_tab_char (feature_col CHAR(10));
CREATE TABLE feature_tab_text (feature_col TEXT);
--
-- Binary Types
CREATE TABLE feature_tab_bytea (feature_col BYTEA);
--
-- Date Time Types
CREATE TABLE feature_tab_timestamp (feature_col TIMESTAMP(2));
CREATE TABLE feature_tab_timestamp_with_time_zone (feature_col TIMESTAMP WITH TIME ZONE);
CREATE TABLE feature_tab_date (feature_col DATE);
CREATE TABLE feature_tab_time (feature_col TIME(2));
CREATE TABLE feature_tab_time_with_time_zone (feature_col TIME(2) WITH TIME ZONE);
CREATE TABLE feature_tab_interval_ym (feature_col INTERVAL YEAR TO MONTH);
CREATE TABLE feature_tab_interval_ds (feature_col INTERVAL DAY TO SECOND(2));
--
-- Boolean Type
CREATE TABLE feature_tab_bool (feature_col BOOLEAN);
--
-- Enumerated Types
CREATE TYPE feature_enum AS ENUM('one', 'two', 'three');
CREATE TABLE feature_tab_enum (feature_col feature_enum);
--
-- Geometric Types
CREATE TABLE feature_tab_point (feature_col POINT);
CREATE TABLE feature_tab_line (feature_col LINE);
CREATE TABLE feature_tab_lseg (feature_col LSEG);
CREATE TABLE feature_tab_box (feature_col BOX);
CREATE TABLE feature_tab_path (feature_col PATH);
CREATE TABLE feature_tab_polygon (feature_col POLYGON);
CREATE TABLE feature_tab_circle (feature_col CIRCLE);
--
-- Network Address Types
CREATE TABLE feature_tab_cidr (feature_col CIDR);
CREATE TABLE feature_tab_inet (feature_col INET);
CREATE TABLE feature_tab_macaddr (feature_col MACADDR);
CREATE TABLE feature_tab_macaddr8 (feature_col MACADDR8);
--
-- Bit String Types
CREATE TABLE feature_tab_bit (feature_col BIT);
CREATE TABLE feature_tab_bit_varying (feature_col BIT VARYING(10));
--
-- Text Search Types
CREATE TABLE feature_tab_tsvector (feature_col TSVECTOR);
CREATE TABLE feature_tab_tsquery (feature_col TSQUERY);
--
-- UUID Type
CREATE TABLE feature_tab_uuid (feature_col UUID);
--
-- XML Type
CREATE TABLE feature_tab_xml (feature_col XML);
--
-- Arrays
CREATE TABLE feature_tab_array_int (feature_col INTEGER[]);
CREATE TABLE feature_tab_array_text (feature_col TEXT[]);
--
-- Composite Types
CREATE TYPE feature_struct AS(id INTEGER, name TEXT);
CREATE TABLE feature_tab_struct (feature_col feature_struct);
--
-- JSON Types
CREATE TABLE feature_tab_json (feature_col JSON);
--
-- Range Types
CREATE TYPE feature_range AS RANGE(subtype=INTEGER);
CREATE TABLE feature_tab_range (feature_col feature_range);
--
-- Domain Types
CREATE DOMAIN feature_domain AS INTEGER CHECK (VALUE > 0);
CREATE TABLE feature_tab_domain (feature_col feature_domain);
--
-- Object Identifier Types
CREATE TABLE feature_tab_oid (feature_col OID);
CREATE TABLE feature_tab_regproc (feature_col REGPROC);
CREATE TABLE feature_tab_regprocedure (feature_col REGPROCEDURE);
CREATE TABLE feature_tab_regoper (feature_col REGOPER);
CREATE TABLE feature_tab_regoperator (feature_col REGOPERATOR);
CREATE TABLE feature_tab_regclass (feature_col REGCLASS);
CREATE TABLE feature_tab_regtype (feature_col REGTYPE);
CREATE TABLE feature_tab_regrole (feature_col REGROLE);
CREATE TABLE feature_tab_regnamespace (feature_col REGNAMESPACE);
CREATE TABLE feature_tab_regconfig (feature_col REGCONFIG);
CREATE TABLE feature_tab_regdictionary (feature_col REGDICTIONARY);
CREATE TABLE feature_tab_xid (feature_col XID);
CREATE TABLE feature_tab_cid (feature_col CID);
CREATE TABLE feature_tab_tid (feature_col TID);
--
-- pg_lsn Type
CREATE TABLE feature_tab_pg_lsn (feature_col PG_LSN);
--
-- Pseudo-Types
CREATE TABLE feature_tab_any (feature_col ANY);
CREATE TABLE feature_tab_anyelement (feature_col ANYELEMENT);
CREATE TABLE feature_tab_anyarray (feature_col ANYARRAY);
CREATE TABLE feature_tab_anynonarray (feature_col ANYNONARRAY);
CREATE TABLE feature_tab_anyenum (feature_col ANYENUM);
CREATE TABLE feature_tab_anyrange (feature_col ANYRANGE);
CREATE TABLE feature_tab_cstring (feature_col CSTRING);
CREATE TABLE feature_tab_internal (feature_col INTERNAL);
CREATE TABLE feature_tab_language_handler (feature_col LANGUAGE_HANDLER);
CREATE TABLE feature_tab_fdw_handler (feature_col FDW_HANDLER);
CREATE TABLE feature_tab_index_am_handler (feature_col INDEX_AM_HANDLER);
CREATE TABLE feature_tab_tsm_handler (feature_col TSM_HANDLER);
CREATE TABLE feature_tab_record (feature_col RECORD);
CREATE TABLE feature_tab_trigger (feature_col TRIGGER);
CREATE TABLE feature_tab_event_trigger (feature_col EVENT_TRIGGER);
CREATE TABLE feature_tab_pg_ddl_command (feature_col PG_DDL_COMMAND);
CREATE TABLE feature_tab_void (feature_col VOID);
-- TODO(jason): uncomment when issue #1975 is closed.
-- CREATE TABLE feature_tab_unknown (feature_col UNKNOWN);
