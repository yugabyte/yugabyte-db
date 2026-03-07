-- =============================================================================
-- Tests for yb_get_tablet_for_key(db_name, table_oid, row_values)
-- Returns the tablet ID that would contain the given key (same-DB and cross-DB).
-- row_values must be a non-NULL record with at least one column.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- 1. Error cases
-- -----------------------------------------------------------------------------

-- Index: must use base table
CREATE TABLE tablet_key_test_idx (k INT PRIMARY KEY, v INT);
CREATE INDEX tablet_key_test_idx_v ON tablet_key_test_idx(v);
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_test_idx_v'::regclass::oid, ROW(1));

-- TODO(#30318): Partitioned parent OID not supported
CREATE TABLE tablet_key_partitioned (k INT PRIMARY KEY, v INT) PARTITION BY RANGE (k);
CREATE TABLE tablet_key_partitioned_p1 PARTITION OF tablet_key_partitioned FOR VALUES FROM (1) TO (100);
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_partitioned'::regclass::oid, ROW(50)) IS NOT NULL as ok;

-- Too many values
CREATE TABLE tablet_key_simple (v INT, k INT PRIMARY KEY);
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_simple'::regclass::oid, ROW(1, 2, 3));

-- NULL in primary key
CREATE TABLE tablet_key_nulltest (v INT, k1 INT, k2 INT, PRIMARY KEY (k1, k2));
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_nulltest'::regclass::oid, ROW(1, NULL::int));

-- NULL in non-primary key (full row with v=NULL) should succeed
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_nulltest'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_nulltest'::regclass::oid, ROW(NULL::int, 1, 2))
) AS null_non_pk_ok;

-- Wrong value count (partial PK not allowed for hash)
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_nulltest'::regclass::oid, ROW(1::int));

-- Type mismatch (wrong type on PK column k, which is PG column 2)
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_simple'::regclass::oid, ROW(2, 'not_an_int'::text));

-- Type mismatch (text passed for timestamptz PK)
CREATE TABLE tbl (a timestamptz PRIMARY KEY, k int, v int);
SELECT yb_get_tablet_for_key(current_database(), 'tbl'::regclass::oid, ROW('not a timestamp'));

-- NULL as the only value (PK column is NULL)
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_simple'::regclass::oid, ROW(NULL));

-- -----------------------------------------------------------------------------
-- 2. Hash table: success and metadata check
-- -----------------------------------------------------------------------------

CREATE TABLE tablet_key_hash (v TEXT, k INT PRIMARY KEY) SPLIT INTO 3 TABLETS;
INSERT INTO tablet_key_hash VALUES ('one', 1), ('hundred', 100), ('thousand', 1000);

-- Returned tablet_id must exist in yb_tablet_metadata for this table
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_hash'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW('one', 1))
) AS ok;

SELECT COUNT(*) BETWEEN 1 AND 3 AS all_in_metadata
FROM yb_tablet_metadata ytm
WHERE ytm.relname = 'tablet_key_hash'
  AND ytm.tablet_id IN (
    yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW('one', 1)),
    yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW('hundred', 100)),
    yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW('thousand', 1000))
  );

-- PK-only (only k): same tablet as full row
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW(1)) =
       yb_get_tablet_for_key(current_database(), 'tablet_key_hash'::regclass::oid, ROW('one', 1))
  AS pk_only_same_tablet;

-- -----------------------------------------------------------------------------
-- 3. Range table: success, distinct tablets, metadata
-- -----------------------------------------------------------------------------

CREATE TABLE tablet_key_range (v TEXT, k INT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((100), (200));
INSERT INTO tablet_key_range VALUES ('first', 50), ('second', 150), ('third', 250);

-- Returned tablet_id must exist in yb_tablet_metadata for this table
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_range'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('first', 50))
) AS ok;

SELECT COUNT(DISTINCT tablet_id) = 3 AS three_tablets
FROM (
  SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('first', 50)) AS tablet_id
  UNION ALL
  SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('second', 150))
  UNION ALL
  SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('third', 250))
) t;

SELECT COUNT(*) = 3 AS all_in_metadata
FROM yb_tablet_metadata ytm
WHERE ytm.relname = 'tablet_key_range'
  AND ytm.tablet_id IN (
    yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('first', 50)),
    yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('second', 150)),
    yb_get_tablet_for_key(current_database(), 'tablet_key_range'::regclass::oid, ROW('third', 250))
  );

-- -----------------------------------------------------------------------------
-- 4. Range + partial PK (prefix key lookup)
-- -----------------------------------------------------------------------------

CREATE TABLE tablet_key_range_composite (
  data TEXT, region TEXT, id INT, ts TIMESTAMP,
  PRIMARY KEY (region ASC, id ASC, ts ASC)
) SPLIT AT VALUES (('east', 1000, '2024-06-01'), ('west', 1000, '2024-06-01'));
INSERT INTO tablet_key_range_composite VALUES
  ('data1', 'alpha', 500, '2024-01-01'),
  ('data2', 'east', 2000, '2024-07-01'),
  ('data3', 'west', 5000, '2024-12-01');

-- Each returned tablet_id must exist in yb_tablet_metadata for this table
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_range_composite'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid, ROW('alpha'))
) AS in_metadata
UNION ALL
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_range_composite'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid, ROW('east', 2000))
)
UNION ALL
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_range_composite'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid,
        ROW('west'::text, 5000, '2024-12-01'::timestamp))
);

SELECT COUNT(*) = 3 AS all_in_metadata
FROM yb_tablet_metadata ytm
WHERE ytm.relname = 'tablet_key_range_composite'
  AND ytm.tablet_id IN (
    yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid, ROW('alpha')),
    yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid, ROW('east', 2000)),
    yb_get_tablet_for_key(current_database(), 'tablet_key_range_composite'::regclass::oid, ROW('west'::text, 5000, '2024-12-01'::timestamp))
  );

-- -----------------------------------------------------------------------------
-- 5a. Hash + range (compound key)
-- -----------------------------------------------------------------------------

CREATE TABLE tablet_key_hash_range (v TEXT, h INT, r INT, PRIMARY KEY (h HASH, r ASC)) SPLIT INTO 3 TABLETS;
INSERT INTO tablet_key_hash_range VALUES ('a', 1, 100), ('b', 2, 200), ('c', 3, 300);

-- Returned tablet_id must exist in yb_tablet_metadata for this table
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_hash_range'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_hash_range'::regclass::oid, ROW('a', 1, 100))
) AS in_metadata;

SELECT COUNT(*) >= 1 AS in_metadata
FROM yb_tablet_metadata ytm
WHERE ytm.relname = 'tablet_key_hash_range'
  AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_hash_range'::regclass::oid, ROW('a', 1, 100));

-- -----------------------------------------------------------------------------
-- 5b. Two hash + two range keys, first column non-PK: valid key prefixes only
-- -----------------------------------------------------------------------------
-- Table: (v, h1, h2, r1, r2), PK ((h1, h2) HASH, r1 ASC, r2 ASC). Values in key order.
-- Range keys can only be given after all hash keys are complete: 0h0r, 1h0r, 2h0r, 2h1r, 2h2r.
CREATE TABLE tablet_key_2h2r (
  v TEXT, h1 INT, h2 INT, r1 INT, r2 INT,
  PRIMARY KEY ((h1, h2) HASH, r1 ASC, r2 ASC)
) SPLIT INTO 2 TABLETS;
INSERT INTO tablet_key_2h2r VALUES ('nopk', 1, 2, 10, 20);

-- 0 hash, 0 range: 0 values
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW());
-- 1 hash, 0 range: 1 value (h1 only)
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW(1));
-- 2 hash, 0 range: 2 values (h1, h2)
-- TODO(#30518): Hash keys in hash-range table not supported.
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW(1, 2));
-- 2 hash, 1 range: 3 values (h1, h2, r1)
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW(1, 2, 10));
-- 2 hash, 2 range: 4 values (full key) - success
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_2h2r'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW(1, 2, 10, 20))
) AS full_key_ok;
-- Full row (5 values: v, h1, h2, r1, r2) - success
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_2h2r'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_2h2r'::regclass::oid, ROW('nopk', 1, 2, 10, 20))
) AS full_row_ok;

-- -----------------------------------------------------------------------------
-- 5c. Multi-column PK with full row in PG order (reordering: PG order != DocDB order)
-- -----------------------------------------------------------------------------
-- Table columns (PG order): a, b, c.  PK is (a, c).  DocDB order: a, c, b.
-- Passing full row in PG order (a, b, c) is reordered to (a, c, b) in pggate.
CREATE TABLE tablet_key_reorder (a INT, b TEXT, c INT, PRIMARY KEY (a, c)) SPLIT INTO 2 TABLETS;
INSERT INTO tablet_key_reorder VALUES (1, 'mid', 2), (10, 'x', 20);

-- PK only (already in DocDB order): returned tablet_id must exist in yb_tablet_metadata
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_reorder'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_reorder'::regclass::oid, ROW(1, 2))
) AS pk_only_ok;

-- Full row in PG definition order (a, b, c) - tests reordering logic
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_reorder'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_reorder'::regclass::oid, ROW(1, 'mid', 2))
) AS full_row_ok;

-- Same key: full row and PK-only must resolve to same tablet
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_reorder'::regclass::oid, ROW(1, 2)) =
       yb_get_tablet_for_key(current_database(), 'tablet_key_reorder'::regclass::oid, ROW(1, 'mid', 2)) AS same_tablet;

-- -----------------------------------------------------------------------------
-- 6. Other types: UUID, TEXT (implicit), TIMESTAMP (3 tablets)
-- -----------------------------------------------------------------------------

CREATE TABLE tablet_key_uuid (data TEXT, id UUID PRIMARY KEY) SPLIT INTO 2 TABLETS;
INSERT INTO tablet_key_uuid VALUES ('data1', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'::uuid);
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_uuid'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_uuid'::regclass::oid,
        ROW('data1', 'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11'::uuid))
) AS ok;

CREATE TABLE tablet_key_text (value INT, name TEXT, PRIMARY KEY (name ASC)) SPLIT AT VALUES (('m'));
INSERT INTO tablet_key_text VALUES (1, 'apple'), (2, 'zebra');
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_text'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_text'::regclass::oid, ROW(1, 'apple'))
) AS ok;
SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_text'::regclass::oid, ROW(1, 'apple')) <>
       yb_get_tablet_for_key(current_database(), 'tablet_key_text'::regclass::oid, ROW(2, 'zebra')) AS different_tablets;

CREATE TABLE tablet_key_ts (v TEXT, ts TIMESTAMP, PRIMARY KEY (ts ASC)) SPLIT AT VALUES (('2024-06-01'), ('2024-09-01'));
INSERT INTO tablet_key_ts VALUES ('a', '2024-03-15'::timestamp), ('b', '2024-07-15'::timestamp), ('c', '2024-11-15'::timestamp);
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'tablet_key_ts'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'tablet_key_ts'::regclass::oid, ROW('a', '2024-03-15'::timestamp))
) AS ok;
SELECT COUNT(DISTINCT tablet_id) = 3 AS three_tablets
FROM (
  SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_ts'::regclass::oid, ROW('a', '2024-03-15'::timestamp)) AS tablet_id
  UNION ALL SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_ts'::regclass::oid, ROW('b', '2024-07-15'::timestamp))
  UNION ALL SELECT yb_get_tablet_for_key(current_database(), 'tablet_key_ts'::regclass::oid, ROW('c', '2024-11-15'::timestamp))
) t;

-- -----------------------------------------------------------------------------
-- 7. Cross-database: success and error cases
-- -----------------------------------------------------------------------------

SELECT current_database() AS orig_db \gset
CREATE DATABASE tablet_key_crossdb_test;
\c tablet_key_crossdb_test

CREATE TABLE crossdb_t (v TEXT, id INT PRIMARY KEY) SPLIT INTO 2 TABLETS;
INSERT INTO crossdb_t VALUES ('a', 1), ('b', 2);
SELECT 'crossdb_t'::regclass::oid AS crossdb_oid \gset

\c :orig_db

-- Cross-db: yb_tablet_metadata filters via JOIN to pg_class (current DB only),
-- so tablets from other DBs are not visible; we only check non-null here.
SELECT yb_get_tablet_for_key('tablet_key_crossdb_test', :crossdb_oid, ROW('a', 1)) IS NOT NULL AS crossdb_ok;

SELECT yb_get_tablet_for_key('nonexistent_database', :crossdb_oid, ROW('a', 1));
SELECT yb_get_tablet_for_key('tablet_key_crossdb_test', :crossdb_oid, ROW('a', 'x'::text));
SELECT yb_get_tablet_for_key('tablet_key_crossdb_test', :crossdb_oid, ROW('a', NULL::int));

\c postgres
DROP DATABASE IF EXISTS tablet_key_crossdb_test;
\c :orig_db

-- -----------------------------------------------------------------------------
-- 8. Partitioned parent table: parent returns tablet_id but it is dummy (GH #30318)
-- See https://github.com/yugabyte/yugabyte-db/issues/30318
-- Child partition OID returns correct tablet_id; parent OID returns non-null but
-- not the actual tablet for the key. This test documents current behavior until
-- the issue is resolved.
-- -----------------------------------------------------------------------------
CREATE TABLE pt_parent_test (v TEXT, k INT, PRIMARY KEY (k)) PARTITION BY RANGE (k);
CREATE TABLE pt_parent_test_p1 PARTITION OF pt_parent_test FOR VALUES FROM (0) TO (100);
INSERT INTO pt_parent_test VALUES ('x', 50);

-- Parent OID: currently returns a tablet_id (dummy; not the actual tablet for k=50)
SELECT yb_get_tablet_for_key(current_database(), 'pt_parent_test'::regclass::oid, ROW(50)) IS NOT NULL AS parent_returns_tablet_id;

-- Child OID: returns correct tablet_id present in yb_tablet_metadata
SELECT EXISTS (
  SELECT 1 FROM yb_tablet_metadata ytm
  WHERE ytm.relname = 'pt_parent_test_p1'
    AND ytm.tablet_id = yb_get_tablet_for_key(current_database(), 'pt_parent_test_p1'::regclass::oid, ROW('x', 50))
) AS child_tablet_in_metadata;

-- -----------------------------------------------------------------------------
-- 9. TODO(#30518): Colocated tables not yet supported
-- -----------------------------------------------------------------------------

CREATE DATABASE tablet_key_colocated_test WITH COLOCATION = true;
\c tablet_key_colocated_test

CREATE TABLE colocated_t (k INT PRIMARY KEY, v TEXT);
SELECT yb_get_tablet_for_key(current_database(), 'colocated_t'::regclass::oid, ROW(1, 'a'));

\c :orig_db
DROP DATABASE IF EXISTS tablet_key_colocated_test;

-- -----------------------------------------------------------------------------
-- 10. Cleanup
-- -----------------------------------------------------------------------------

DROP TABLE IF EXISTS tablet_key_test_idx CASCADE;
DROP TABLE IF EXISTS tablet_key_partitioned CASCADE;
DROP TABLE IF EXISTS tablet_key_simple CASCADE;
DROP TABLE IF EXISTS tablet_key_nulltest CASCADE;
DROP TABLE IF EXISTS tbl CASCADE;
DROP TABLE IF EXISTS tablet_key_hash CASCADE;
DROP TABLE IF EXISTS tablet_key_range CASCADE;
DROP TABLE IF EXISTS tablet_key_range_composite CASCADE;
DROP TABLE IF EXISTS tablet_key_hash_range CASCADE;
DROP TABLE IF EXISTS tablet_key_2h2r CASCADE;
DROP TABLE IF EXISTS tablet_key_reorder CASCADE;
DROP TABLE IF EXISTS tablet_key_uuid CASCADE;
DROP TABLE IF EXISTS tablet_key_text CASCADE;
DROP TABLE IF EXISTS tablet_key_ts CASCADE;
DROP TABLE IF EXISTS pt_parent_test CASCADE;
