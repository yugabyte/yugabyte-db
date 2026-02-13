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

-- Test that active_sst_sizes and wal_sizes columns are present and aligned with replicas
SELECT
    relname,
    pg_typeof(active_sst_sizes) AS sst_type,
    pg_typeof(wal_sizes) AS wal_type,
    array_length(active_sst_sizes, 1) = array_length(replicas, 1) AS sst_match,
    array_length(wal_sizes, 1) = array_length(replicas, 1) AS wal_match
FROM yb_tablet_metadata
WHERE relname = 'test_table_1'
ORDER BY start_hash_code NULLS FIRST
LIMIT 1;

-- Test that all size values are non-negative for each replica
SELECT r.val AS replica, s.val AS sst_size, w.val AS wal_size
FROM yb_tablet_metadata tm,
     unnest(tm.replicas)         WITH ORDINALITY AS r(val, ord),
     unnest(tm.active_sst_sizes) WITH ORDINALITY AS s(val, ord),
     unnest(tm.wal_sizes)        WITH ORDINALITY AS w(val, ord)
WHERE tm.relname = 'test_table_1'
  AND r.ord = s.ord AND r.ord = w.ord
  AND (s.val < 0 OR w.val < 0);

-- Test that every replica address corresponds to a live tserver
SELECT unnest(replicas) AS orphan_replica
FROM yb_tablet_metadata
WHERE relname = 'test_table_1'
EXCEPT
SELECT host || ':' || port FROM yb_servers();

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
