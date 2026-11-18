-- Default set of tests for TOAST compression, independent on compression
-- methods supported by the build.

CREATE SCHEMA pglz;
SET search_path TO pglz, public;
\set HIDE_TOAST_COMPRESSION false

-- ensure we get stable results regardless of installation's default
SET default_toast_compression = 'pglz';

-- test creating table with compression method
CREATE TEMP TABLE cmdata(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
CREATE INDEX idx ON cmdata(f1);
INSERT INTO cmdata VALUES(repeat('1234567890', 1000));
-- YB: \d+ of a temp table has unstable output as the temporary schema name
-- contains the tserver uuid and the backend number. Use regexp_replace to
-- change it to pg_temp_x and drop the title centering so that the result
-- is stable.
select current_setting('data_directory') || 'describe.out' as desc_output_file -- YB remove temp table schema name UUID
\gset
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID

-- verify stored compression method in the data
SELECT pg_column_compression(f1) FROM cmdata;

-- decompress data slice
SELECT SUBSTR(f1, 200, 5) FROM cmdata;

-- copy with table creation
SELECT * INTO cmmove1 FROM cmdata;
\d+ cmmove1
SELECT pg_column_compression(f1) FROM cmmove1;

-- try setting compression for incompressible data type
CREATE TEMP TABLE cmdata2 (f1 int COMPRESSION pglz); -- YB use temp tables instead of regular tables

-- test externally stored compressed data
CREATE OR REPLACE FUNCTION large_val() RETURNS TEXT LANGUAGE SQL AS
'select array_agg(fipshash(g::text))::text from generate_series(1, 256) g';
CREATE TEMP TABLE cmdata2 (f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmdata2 SELECT large_val() || repeat('a', 4000);
SELECT pg_column_compression(f1) FROM cmdata2;
SELECT SUBSTR(f1, 200, 5) FROM cmdata2;
DROP TABLE cmdata2;

--test column type update varlena/non-varlena
CREATE TEMP TABLE cmdata2 (f1 int); -- YB use temp tables instead of regular tables
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE varchar;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE int USING f1::integer;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID

--changing column storage should not impact the compression method
--but the data should not be compressed
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE varchar;
ALTER TABLE cmdata2 ALTER COLUMN f1 SET COMPRESSION pglz;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID
ALTER TABLE cmdata2 ALTER COLUMN f1 SET STORAGE plain;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID
INSERT INTO cmdata2 VALUES (repeat('123456789', 800));
SELECT pg_column_compression(f1) FROM cmdata2;

-- test compression with inheritance
CREATE TEMP TABLE cmdata3(f1 text); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cminh() INHERITS (cmdata, cmdata3); -- YB use temp tables instead of regular tables

-- test default_toast_compression GUC
-- suppress machine-dependent details
\set VERBOSITY terse
SET default_toast_compression = '';
SET default_toast_compression = 'I do not exist compression';
SET default_toast_compression = 'pglz';
\set VERBOSITY default

ALTER TABLE cmdata2 ALTER COLUMN f1 SET COMPRESSION default;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID

DROP TABLE cmdata2;

-- VACUUM FULL does not recompress
SELECT pg_column_compression(f1) FROM cmdata;
VACUUM FULL cmdata;
SELECT pg_column_compression(f1) FROM cmdata;

-- check data is ok
SELECT length(f1) FROM cmdata;
SELECT length(f1) FROM cmmove1;

CREATE TEMP TABLE badcompresstbl (a text COMPRESSION I_Do_Not_Exist_Compression); -- fails, YB use temp tables instead of regular tables
CREATE TEMP TABLE badcompresstbl (a text); -- YB use temp tables instead of regular tables
ALTER TABLE badcompresstbl ALTER a SET COMPRESSION I_Do_Not_Exist_Compression; -- fails
DROP TABLE badcompresstbl;

\set HIDE_TOAST_COMPRESSION true
