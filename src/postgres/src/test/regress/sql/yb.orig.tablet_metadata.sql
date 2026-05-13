-- Test that data from multiple tables is returned
CREATE TABLE test_table_1 (k INT PRIMARY KEY, v INT) SPLIT INTO 2 TABLETS;
CREATE TABLE test_table_2 (k INT, v INT, PRIMARY KEY (k asc));

SELECT
    relname,
    db_name,
    start_hash_code,
    end_hash_code
FROM yb_tablet_metadata WHERE relname IN ('test_table_1', 'test_table_2')
ORDER BY start_hash_code NULLS FIRST;

-- Test that tablet_attrs column contains expected JSON keys
SELECT
    relname,
    tablet_attrs ? 'sst_files_disk_size' AS has_sst_size,
    tablet_attrs ? 'wal_files_disk_size' AS has_wal_size,
    tablet_attrs ? 'uncompressed_sst_files_disk_size' AS has_uncompressed_sst_size,
    (tablet_attrs->>'sst_files_disk_size')::bigint >= 0 AS sst_size_non_negative,
    (tablet_attrs->>'wal_files_disk_size')::bigint >= 0 AS wal_size_non_negative,
    (tablet_attrs->>'uncompressed_sst_files_disk_size')::bigint >= 0 AS uncompressed_sst_size_non_negative
FROM yb_tablet_metadata WHERE relname = 'test_table_1'
ORDER BY start_hash_code NULLS FIRST;

-- Test that we are able to join with yb_servers()
SELECT
    ytm.relname,
    ytm.db_name,
    ytm.start_hash_code,
    ytm.end_hash_code,
    ys.cloud,
    ys.region,
    ys.zone
FROM yb_tablet_metadata ytm
JOIN yb_servers() ys
    ON split_part(ytm.leader, ':', 1) = ys.host
    AND split_part(ytm.leader, ':', 2)::int = ys.port
WHERE ytm.relname IN ('test_table_1', 'test_table_2')
ORDER BY ytm.start_hash_code NULLS FIRST;

-- Test that data from multiple databases is returned
CREATE DATABASE test_db;
\c test_db

CREATE TABLE test_table_1 (k INT PRIMARY KEY, v INT) SPLIT INTO 2 TABLETS;
CREATE TABLE test_table_2 (k INT, v INT, PRIMARY KEY (k asc));

SELECT
    relname,
    db_name,
    start_hash_code,
    end_hash_code
FROM yb_tablet_metadata
WHERE
    relname IN ('test_table_1', 'test_table_2')
    AND db_name IN ('test_db', 'yugabyte')
ORDER BY db_name, start_hash_code NULLS FIRST;

-- Test that data from colocated tables is returned
CREATE DATABASE colocated_db WITH COLOCATION = true;
\c colocated_db

CREATE TABLE test_table_1 (k INT PRIMARY KEY, v INT);
CREATE TABLE test_table_2 (k INT, v INT, PRIMARY KEY (k asc));
CREATE TABLE test_table_3 (k INT PRIMARY KEY, v INT) WITH (COLOCATION = false) SPLIT INTO 2 TABLETS;

SELECT
    relname,
    db_name,
    start_hash_code,
    end_hash_code
FROM yb_tablet_metadata
WHERE
    relname IN ('test_table_1', 'test_table_2', 'test_table_3')
    AND db_name = 'colocated_db'
ORDER BY start_hash_code NULLS FIRST;
