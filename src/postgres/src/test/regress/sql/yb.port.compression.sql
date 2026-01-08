\set HIDE_TOAST_COMPRESSION false

-- ensure we get stable results regardless of installation's default
SET default_toast_compression = 'pglz';

-- test creating table with compression method
CREATE TEMP TABLE cmdata(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
CREATE INDEX idx ON cmdata(f1);
INSERT INTO cmdata VALUES(repeat('1234567890', 1000));
-- YB: \d temptest has unstable output as the temporary schemaname contains
-- the tserver uuid. Use regexp_replace to change it to pg_temp_x so that the
-- result is stable.
select current_setting('data_directory') || 'describe.out' as desc_output_file -- YB remove temp table schema name UUID
\gset
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');              

CREATE TEMP TABLE cmdata1(f1 TEXT COMPRESSION lz4); -- YB use temp tables instead of regular tables
INSERT INTO cmdata1 VALUES(repeat('1234567890', 1004));
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata1
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');

-- verify stored compression method in the data
SELECT pg_column_compression(f1) FROM cmdata;
SELECT pg_column_compression(f1) FROM cmdata1;

-- decompress data slice
SELECT SUBSTR(f1, 200, 5) FROM cmdata;
SELECT SUBSTR(f1, 2000, 50) FROM cmdata1;

-- copy with table creation
SELECT * INTO cmmove1 FROM cmdata;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmmove1
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
SELECT pg_column_compression(f1) FROM cmmove1;

-- copy to existing table
CREATE TEMP TABLE cmmove3(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmmove3 SELECT * FROM cmdata;
INSERT INTO cmmove3 SELECT * FROM cmdata1;
SELECT pg_column_compression(f1) FROM cmmove3;

-- test LIKE INCLUDING COMPRESSION
CREATE TEMP TABLE cmdata2 (LIKE cmdata1 INCLUDING COMPRESSION); -- YB use temp tables instead of regular tables
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
DROP TABLE cmdata2;

-- try setting compression for incompressible data type
CREATE TEMP TABLE cmdata2 (f1 int COMPRESSION pglz); -- YB use temp tables instead of regular tables

-- update using datum from different table
CREATE TEMP TABLE cmmove2(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmmove2 VALUES (repeat('1234567890', 1004));
SELECT pg_column_compression(f1) FROM cmmove2;
UPDATE cmmove2 SET f1 = cmdata1.f1 FROM cmdata1;
SELECT pg_column_compression(f1) FROM cmmove2;

-- test externally stored compressed data
CREATE OR REPLACE FUNCTION large_val() RETURNS TEXT LANGUAGE SQL AS
'select array_agg(md5(g::text))::text from generate_series(1, 256) g';
CREATE TEMP TABLE cmdata2 (f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables
INSERT INTO cmdata2 SELECT large_val() || repeat('a', 4000);
SELECT pg_column_compression(f1) FROM cmdata2;
INSERT INTO cmdata1 SELECT large_val() || repeat('a', 4000);
SELECT pg_column_compression(f1) FROM cmdata1;
SELECT SUBSTR(f1, 200, 5) FROM cmdata1;
SELECT SUBSTR(f1, 200, 5) FROM cmdata2;
DROP TABLE cmdata2;

--test column type update varlena/non-varlena
CREATE TEMP TABLE cmdata2 (f1 int); -- YB use temp tables instead of regular tables
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE varchar;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE int USING f1::integer;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');

--changing column storage should not impact the compression method
--but the data should not be compressed
ALTER TABLE cmdata2 ALTER COLUMN f1 TYPE varchar;
ALTER TABLE cmdata2 ALTER COLUMN f1 SET COMPRESSION pglz;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
ALTER TABLE cmdata2 ALTER COLUMN f1 SET STORAGE plain;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
INSERT INTO cmdata2 VALUES (repeat('123456789', 800));
SELECT pg_column_compression(f1) FROM cmdata2;

/* YB: skip materialized view compression tests as they only apply to regular tables
-- test compression with materialized view
CREATE MATERIALIZED VIEW compressmv(x) AS SELECT * FROM cmdata1;
\d+ compressmv
SELECT pg_column_compression(f1) FROM cmdata1;
SELECT pg_column_compression(x) FROM compressmv;
YB */

-- test compression with partition
CREATE TEMP TABLE cmpart(f1 text COMPRESSION lz4) PARTITION BY HASH(f1); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cmpart1 PARTITION OF cmpart FOR VALUES WITH (MODULUS 2, REMAINDER 0); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cmpart2(f1 text COMPRESSION pglz); -- YB use temp tables instead of regular tables

ALTER TABLE cmpart ATTACH PARTITION cmpart2 FOR VALUES WITH (MODULUS 2, REMAINDER 1);
INSERT INTO cmpart VALUES (repeat('123456789', 1004));
INSERT INTO cmpart VALUES (repeat('123456789', 4004));
SELECT pg_column_compression(f1) FROM cmpart1;
SELECT pg_column_compression(f1) FROM cmpart2;

-- test compression with inheritance, error
CREATE TEMP TABLE cminh() INHERITS(cmdata, cmdata1); -- YB use temp tables instead of regular tables
CREATE TEMP TABLE cminh(f1 TEXT COMPRESSION lz4) INHERITS(cmdata); -- YB use temp tables instead of regular tables

-- test default_toast_compression GUC
SET default_toast_compression = '';
SET default_toast_compression = 'I do not exist compression';
SET default_toast_compression = 'lz4';
SET default_toast_compression = 'pglz';

-- test alter compression method
ALTER TABLE cmdata ALTER COLUMN f1 SET COMPRESSION lz4;
INSERT INTO cmdata VALUES (repeat('123456789', 4004));
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');
SELECT pg_column_compression(f1) FROM cmdata;

ALTER TABLE cmdata2 ALTER COLUMN f1 SET COMPRESSION default;
\o :desc_output_file \\ -- YB remove temp table schema name UUID
\d+ cmdata2
\o \\ -- YB remove temp table schema name UUID
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');


/* YB: skip materialized view compression tests as they only apply to regular tables.
-- test alter compression method for materialized views
ALTER MATERIALIZED VIEW compressmv ALTER COLUMN x SET COMPRESSION lz4;
\d+ compressmv
YB: */

-- test alter compression method for partitioned tables
ALTER TABLE cmpart1 ALTER COLUMN f1 SET COMPRESSION pglz;
ALTER TABLE cmpart2 ALTER COLUMN f1 SET COMPRESSION lz4;

-- new data should be compressed with the current compression method
INSERT INTO cmpart VALUES (repeat('123456789', 1004));
INSERT INTO cmpart VALUES (repeat('123456789', 4004));
SELECT pg_column_compression(f1) FROM cmpart1;
SELECT pg_column_compression(f1) FROM cmpart2;

-- VACUUM FULL does not recompress
SELECT pg_column_compression(f1) FROM cmdata;
VACUUM FULL cmdata;
SELECT pg_column_compression(f1) FROM cmdata;

-- test expression index
DROP TABLE cmdata2;
CREATE TEMP TABLE cmdata2 (f1 TEXT COMPRESSION pglz, f2 TEXT COMPRESSION lz4); -- YB use temp tables instead of regular tables
CREATE UNIQUE INDEX idx1 ON cmdata2 ((f1 || f2));
INSERT INTO cmdata2 VALUES((SELECT array_agg(md5(g::TEXT))::TEXT FROM
generate_series(1, 50) g), VERSION());

-- check data is ok
SELECT length(f1) FROM cmdata;
SELECT length(f1) FROM cmdata1;
SELECT length(f1) FROM cmmove1;
SELECT length(f1) FROM cmmove2;
SELECT length(f1) FROM cmmove3;

CREATE TEMP TABLE badcompresstbl (a text COMPRESSION I_Do_Not_Exist_Compression); -- fails, YB use temp tables instead of regular tables
CREATE TEMP TABLE badcompresstbl (a text); -- YB use temp tables instead of regular tables
ALTER TABLE badcompresstbl ALTER a SET COMPRESSION I_Do_Not_Exist_Compression; -- fails, YB use temp tables instead of regular tables
DROP TABLE badcompresstbl;

\set HIDE_TOAST_COMPRESSION true
