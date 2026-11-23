-- Tests for TOAST compression with lz4

SELECT NOT(enumvals @> '{lz4}') AS skip_test FROM pg_settings WHERE
  name = 'default_toast_compression' \gset
\if :skip_test
   \echo '*** skipping TOAST tests with lz4 (not supported) ***'
   \quit
\endif

CREATE SCHEMA lz4;
SET search_path TO lz4, public;

\set HIDE_TOAST_COMPRESSION false

-- Ensure we get stable results regardless of the installation's default.
-- We rely on this GUC value for a few tests.
SET default_toast_compression = 'pglz';

-- test creating table with compression method
CREATE TEMP TABLE cmdata_pglz(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
CREATE INDEX idx ON cmdata_pglz(f1);
INSERT INTO cmdata_pglz VALUES(repeat('1234567890', 1000));
\d+ cmdata
CREATE TEMP TABLE cmdata_lz4(f1 TEXT COMPRESSION lz4); -- YB use temp tables instead of regular tables
INSERT INTO cmdata_lz4 VALUES(repeat('1234567890', 1004));
\d+ cmdata1

-- verify stored compression method in the data
SELECT pg_column_compression(f1) FROM cmdata_lz4;

-- decompress data slice
SELECT SUBSTR(f1, 200, 5) FROM cmdata_pglz;
SELECT SUBSTR(f1, 2000, 50) FROM cmdata_lz4;

-- copy with table creation
SELECT * INTO cmmove1 FROM cmdata_lz4;
\d+ cmmove1
SELECT pg_column_compression(f1) FROM cmmove1;

-- test LIKE INCLUDING COMPRESSION.  The GUC default_toast_compression
-- has no effect, the compression method from the table being copied.
CREATE TEMP TABLE cmdata2 (LIKE cmdata_lz4 INCLUDING COMPRESSION); -- YB use temp tables instead of regular tables
-- YB: \d+ of a temp table has unstable output as the temporary schema name
-- contains the tserver uuid and the backend number. Use regexp_replace to
-- change it to pg_temp_x and drop the title centering so that the result
-- is stable.
select current_setting('data_directory') || 'describe.out' as desc_output_file -- YB remove temp table schema name UUID
\gset
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), ' *Table "pg_temp_.{32}_\d+', 'Table "pg_temp_x', 'g'); -- YB remove temp table schema name UUID
DROP TABLE cmdata2;

-- copy to existing table
CREATE TEMP TABLE cmmove3(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmmove3 SELECT * FROM cmdata_pglz;
INSERT INTO cmmove3 SELECT * FROM cmdata_lz4;
SELECT pg_column_compression(f1) FROM cmmove3;

-- update using datum from different table with LZ4 data.
CREATE TEMP TABLE cmmove2(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmmove2 VALUES (repeat('1234567890', 1004));
SELECT pg_column_compression(f1) FROM cmmove2;
UPDATE cmmove2 SET f1 = cmdata_lz4.f1 FROM cmdata_lz4;
SELECT pg_column_compression(f1) FROM cmmove2;

-- test externally stored compressed data
CREATE OR REPLACE FUNCTION large_val_lz4() RETURNS TEXT LANGUAGE SQL AS
'select array_agg(fipshash(g::text))::text from generate_series(1, 256) g';
CREATE TEMP TABLE cmdata2 (f1 text COMPRESSION lz4); -- YB use temp tables instead of regular tables
INSERT INTO cmdata2 SELECT large_val_lz4() || repeat('a', 4000);
SELECT pg_column_compression(f1) FROM cmdata2;
SELECT SUBSTR(f1, 200, 5) FROM cmdata2;
DROP TABLE cmdata2;
DROP FUNCTION large_val_lz4;

/* YB: skip materialized view compression tests as they only apply to regular tables
-- test compression with materialized view
CREATE MATERIALIZED VIEW compressmv(x) AS SELECT * FROM cmdata_lz4;
\d+ compressmv
SELECT pg_column_compression(f1) FROM cmdata_lz4;
SELECT pg_column_compression(x) FROM compressmv;
*/ -- YB

-- test compression with partition
CREATE TEMP TABLE cmpart(f1 text COMPRESSION lz4) PARTITION BY HASH(f1); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cmpart1 PARTITION OF cmpart FOR VALUES WITH (MODULUS 2, REMAINDER 0); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cmpart2(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables

ALTER TABLE cmpart ATTACH PARTITION cmpart2 FOR VALUES WITH (MODULUS 2, REMAINDER 1);
INSERT INTO cmpart VALUES (repeat('123456789', 1004));
INSERT INTO cmpart VALUES (repeat('123456789', 4004));
SELECT pg_column_compression(f1) FROM cmpart1;
SELECT pg_column_compression(f1) FROM cmpart2;

-- test compression with inheritance
CREATE TEMP TABLE cminh() INHERITS(cmdata_pglz, cmdata_lz4); -- error, YB use temp tables instead of regular tables
CREATE TEMP TABLE cminh(f1 TEXT COMPRESSION lz4) INHERITS(cmdata_pglz); -- error, YB use temp tables instead of regular tables
CREATE TEMP TABLE cmdata3(f1 text); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cminh() INHERITS (cmdata_pglz, cmdata3); -- YB use temp tables instead of regular tables

-- test default_toast_compression GUC
SET default_toast_compression = 'lz4';

-- test alter compression method
ALTER TABLE cmdata_pglz ALTER COLUMN f1 SET COMPRESSION lz4;
INSERT INTO cmdata_pglz VALUES (repeat('123456789', 4004));
\d+ cmdata
SELECT pg_column_compression(f1) FROM cmdata_pglz;
ALTER TABLE cmdata_pglz ALTER COLUMN f1 SET COMPRESSION pglz;

/* YB: skip materialized view compression tests as they only apply to regular tables
-- test alter compression method for materialized views
ALTER MATERIALIZED VIEW compressmv ALTER COLUMN x SET COMPRESSION lz4;
\d+ compressmv
*/ -- YB

-- test alter compression method for partitioned tables
ALTER TABLE cmpart1 ALTER COLUMN f1 SET COMPRESSION pglz;
ALTER TABLE cmpart2 ALTER COLUMN f1 SET COMPRESSION lz4;

-- new data should be compressed with the current compression method
INSERT INTO cmpart VALUES (repeat('123456789', 1004));
INSERT INTO cmpart VALUES (repeat('123456789', 4004));
SELECT pg_column_compression(f1) FROM cmpart1;
SELECT pg_column_compression(f1) FROM cmpart2;

-- test expression index
CREATE TEMP TABLE cmdata2 (f1 TEXT COMPRESSION pglz, f2 TEXT COMPRESSION lz4); -- YB use temp tables instead of regular tables
CREATE UNIQUE INDEX idx1 ON cmdata2 ((f1 || f2));
INSERT INTO cmdata2 VALUES((SELECT array_agg(fipshash(g::TEXT))::TEXT FROM
generate_series(1, 50) g), VERSION());

-- check data is ok
SELECT length(f1) FROM cmdata_pglz;
SELECT length(f1) FROM cmdata_lz4;
SELECT length(f1) FROM cmmove1;
SELECT length(f1) FROM cmmove2;
SELECT length(f1) FROM cmmove3;

\set HIDE_TOAST_COMPRESSION true
