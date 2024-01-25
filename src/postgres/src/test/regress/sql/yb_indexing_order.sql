CREATE OR REPLACE FUNCTION get_table_indexes(table_name text)
RETURNS TABLE(
    relname name,
    indisprimary boolean,
    indisunique boolean,
    indexdef text,
    constraintdef text
) AS $$
BEGIN
    RETURN QUERY EXECUTE
    'SELECT c2.relname, i.indisprimary, i.indisunique, pg_catalog.pg_get_indexdef(i.indexrelid, 0, true), ' ||
    'pg_catalog.pg_get_constraintdef(con.oid, true) ' ||
    'FROM pg_catalog.pg_class c, pg_catalog.pg_class c2, pg_catalog.pg_index i ' ||
    'LEFT JOIN pg_catalog.pg_constraint con ON (conrelid = i.indrelid AND conindid = i.indexrelid AND contype IN (''p'',''u'',''x'')) ' ||
    'WHERE c.oid = ' || quote_literal(table_name) || '::regclass AND c.oid = i.indrelid AND i.indexrelid = c2.oid ' ||
    'ORDER BY i.indisprimary DESC, i.indisunique DESC, c2.relname';
END;
$$ LANGUAGE plpgsql;

SET yb_use_hash_splitting_by_default = true; -- should default to true
CREATE TABLE hash_table(a int primary key, b int, c int); -- column a should be HASH
SELECT * FROM get_table_indexes('hash_table');

CREATE TABLE hash_with_asc(a int, b int, c int, primary key(a, b)); -- column a HASH column b ASC
SELECT * FROM get_table_indexes('hash_with_asc');

CREATE TABLE mixed_hash_with_asc(a int, b int, c int, primary key((a,b) HASH, c)); -- column a+b HASH column c ASC
SELECT * FROM get_table_indexes('mixed_hash_with_asc');

CREATE TABLE multi_index_hash_default(a int, b int, c int);

ALTER TABLE multi_index_hash_default ADD PRIMARY KEY(a); -- a HASH
SELECT * FROM get_table_indexes('multi_index_hash_default');
ALTER TABLE multi_index_hash_default DROP CONSTRAINT multi_index_hash_default_pkey;

ALTER TABLE multi_index_hash_default ADD PRIMARY KEY(a,b); -- a HASH b ASC
SELECT * FROM get_table_indexes('multi_index_hash_default');
ALTER TABLE multi_index_hash_default DROP CONSTRAINT multi_index_hash_default_pkey;

ALTER TABLE multi_index_hash_default ADD PRIMARY KEY(a,b ASC);
SELECT * FROM get_table_indexes('multi_index_hash_default');
ALTER TABLE multi_index_hash_default DROP CONSTRAINT multi_index_hash_default_pkey;

ALTER TABLE multi_index_hash_default ADD PRIMARY KEY(a,b HASH); -- error

ALTER TABLE multi_index_hash_default ADD PRIMARY KEY(a, b);

CREATE INDEX ON multi_index_hash_default (b); -- HASH
CREATE INDEX ON multi_index_hash_default (b HASH);
CREATE INDEX ON multi_index_hash_default (c ASC);
CREATE INDEX ON multi_index_hash_default ((a, c), b);
CREATE INDEX ON multi_index_hash_default ((a, c));
CREATE INDEX ON multi_index_hash_default (a, b);
CREATE INDEX ON multi_index_hash_default (b, c ASC);
CREATE INDEX ON multi_index_hash_default (b, c HASH); -- error

SELECT * FROM get_table_indexes('multi_index_hash_default');

-- With HASH as default, creating a table or index with SPLIT AT should fail
CREATE INDEX ON multi_index_hash_default (c) SPLIT AT VALUES((10), (20), (30));
CREATE TABLE split_fail(a int primary key, b int) SPLIT AT VALUES ((1000),(2000),(3000));

-- Confirm that create table like creates the same sort order even after toggling yb_use_hash_splitting_by_default
SET yb_use_hash_splitting_by_default = false;

CREATE TABLE like_hash_default_table(like multi_index_hash_default including all);

-- Duplicate indexes (indexes on the same key) will not have been created, but existing indexes should have
-- the same sort order
SELECT * FROM get_table_indexes('like_hash_default_table');

-- Will be recreated again later
DROP TABLE mixed_hash_with_asc;

CREATE TABLE asc_table(a int primary key, b int, c int); -- column a should be ASC
SELECT * FROM get_table_indexes('asc_table');

CREATE TABLE asc_two_column_table(a int, b int, c int, primary key(a, b)); -- column a ASC column b ASC
SELECT * FROM get_table_indexes('asc_two_column_table');

CREATE TABLE mixed_hash_with_asc(a int, b int, c int, primary key((a,b) HASH, c)); -- column a+b HASH column c ASC
SELECT * FROM get_table_indexes('mixed_hash_with_asc');

CREATE TABLE multi_index_asc_default(a int, b int, c int);

ALTER TABLE multi_index_asc_default ADD PRIMARY KEY(a); -- a ASC
SELECT * FROM get_table_indexes('multi_index_asc_default');
ALTER TABLE multi_index_asc_default DROP CONSTRAINT multi_index_asc_default_pkey;

ALTER TABLE multi_index_asc_default ADD PRIMARY KEY(a,b); -- a ASC b ASC
SELECT * FROM get_table_indexes('multi_index_asc_default');
ALTER TABLE multi_index_asc_default DROP CONSTRAINT multi_index_asc_default_pkey;

ALTER TABLE multi_index_asc_default ADD PRIMARY KEY(a,b ASC);
SELECT * FROM get_table_indexes('multi_index_asc_default');
ALTER TABLE multi_index_asc_default DROP CONSTRAINT multi_index_asc_default_pkey;

ALTER TABLE multi_index_asc_default ADD PRIMARY KEY(a,b HASH); -- error

ALTER TABLE multi_index_asc_default ADD PRIMARY KEY(a, b);

CREATE INDEX ON multi_index_asc_default (b); -- ASC
CREATE INDEX ON multi_index_asc_default (b HASH);
CREATE INDEX ON multi_index_asc_default (c ASC);
CREATE INDEX ON multi_index_asc_default ((a, c), b);
CREATE INDEX ON multi_index_asc_default ((a, c));
CREATE INDEX ON multi_index_asc_default (a, b);
CREATE INDEX ON multi_index_asc_default (b, c ASC);
CREATE INDEX ON multi_index_asc_default (b, c HASH); -- error

SELECT * FROM get_table_indexes('multi_index_asc_default');

-- With ASC as default, creating a table with SPLIT AT should succeed
CREATE TABLE split_table(a int primary key, b int) SPLIT AT VALUES ((1000),(2000),(3000));
CREATE INDEX ON split_table (b) SPLIT AT VALUES((10), (20), (30));

SELECT * FROM get_table_indexes('split_table');

-- Confirm that create table like creates the same sort order even after toggling yb_use_hash_splitting_by_default
SET yb_use_hash_splitting_by_default = true;

CREATE TABLE like_asc_default_table(like multi_index_asc_default including all);

-- Duplicate indexes (indexes on the same key) will not have been created, but existing indexes should have
-- the same sort order
SELECT * FROM get_table_indexes('like_asc_default_table');

SET yb_use_hash_splitting_by_default = false;

-- Partitioned table
CREATE TABLE range_partitioned_example
(
    id            serial,
    date_recorded date,
    data          text,
    PRIMARY KEY (id, date_recorded)
) PARTITION BY RANGE (date_recorded);

CREATE TABLE range_partitioned_example_y2023 PARTITION OF range_partitioned_example
    FOR VALUES FROM ('2023-01-01') TO ('2024-01-01');

CREATE TABLE range_partitioned_example_y2024 PARTITION OF range_partitioned_example
    FOR VALUES FROM ('2024-01-01') TO ('2025-01-01');

CREATE INDEX ON range_partitioned_example (date_recorded);

SELECT * FROM get_table_indexes('range_partitioned_example');
SELECT * FROM get_table_indexes('range_partitioned_example_y2023');
SELECT * FROM get_table_indexes('range_partitioned_example_y2024');

-- GIN
CREATE TABLE gin_example (id serial PRIMARY KEY, data jsonb);

CREATE INDEX data_gin ON gin_example USING gin(data);

SELECT * FROM get_table_indexes('gin_example');

-- Temp table
CREATE TEMP TABLE temp_example (id serial, name text, value int, primary key(id HASH)); -- fails

CREATE TEMP TABLE temp_example (id serial, name text, value int, primary key(id, name HASH)); -- fails

CREATE TEMP TABLE temp_example (id serial, name text, value int, primary key((id, name), value)); -- fails

CREATE TEMP TABLE temp_example (id serial PRIMARY KEY, name text, value int);

CREATE INDEX temp_value_lsm ON temp_example USING lsm(value); -- fails
CREATE INDEX temp_value_idx ON temp_example (value);

SELECT * FROM get_table_indexes('temp_example');

CREATE TABLE foo(a int, b int, c int, primary key (a HASH, b HASH, c ASC));
CREATE TABLE foo(a int, b int, c int, primary key (a, b HASH, c));
CREATE TABLE foo(a int, b int, c int, primary key ((a, b) ASC, c));

-- Creating this table with 3 tablets should not work when yb_use_hash_splitting_by_default = false
-- because the primary key is a range partition.
CREATE TABLE foo(a int primary key, b int) SPLIT (INTO 3 TABLETS); -- fails
CREATE TABLE foo(a int, b int, primary key(a)) SPLIT (INTO 3 TABLETS); -- fails
CREATE TABLE foo(a int, b int, primary key(a HASH)) SPLIT (INTO 3 TABLETS); -- succeeds

SET yb_use_hash_splitting_by_default = true;

CREATE TABLE bar(a int primary key, b int) SPLIT (INTO 3 TABLETS); -- succeeds
CREATE TABLE baz(a int, b int, primary key(a)) SPLIT (INTO 3 TABLETS); -- succeeds

DROP TABLE foo, bar, baz;

-- TABLEGROUP tests
CREATE TABLEGROUP postgres_ordering;

SET yb_use_hash_splitting_by_default = false;

CREATE TABLE tg_hash_key (a int, b int, c int, primary key((a,b) HASH, c)) TABLEGROUP postgres_ordering; -- fails
CREATE TABLE tg_hash_key (a int, b int, c int, primary key((a,b), c)) TABLEGROUP postgres_ordering; -- fails
CREATE TABLE tg_hash_key (a int, b int, c int, primary key(a HASH)) TABLEGROUP postgres_ordering; -- fails

CREATE TABLE tablegroup_table (a int, b int, c int, primary key(a)) TABLEGROUP postgres_ordering; -- succeeds

SELECT * FROM get_table_indexes('tablegroup_table');

ALTER TABLE tablegroup_table DROP CONSTRAINT tablegroup_table_pkey;
ALTER TABLE tablegroup_table ADD PRIMARY KEY (a);

CREATE INDEX ON tablegroup_table (b); -- ASC
CREATE INDEX ON tablegroup_table (b HASH); -- error
CREATE INDEX ON tablegroup_table (c ASC);
CREATE INDEX ON tablegroup_table ((a, c), b); -- error
CREATE INDEX ON tablegroup_table ((a, c)); --error
CREATE INDEX ON tablegroup_table (a, b);
CREATE INDEX ON tablegroup_table (b, c ASC);
CREATE INDEX ON tablegroup_table (b, c HASH); -- error

SELECT * FROM get_table_indexes('tablegroup_table');

CREATE TABLE like_tablegroup_table (like tablegroup_table including all) TABLEGROUP postgres_ordering;

SELECT * FROM get_table_indexes('like_tablegroup_table');

SET yb_use_hash_splitting_by_default = true;

CREATE TABLE like_tablegroup_table2 (like tablegroup_table including all) TABLEGROUP postgres_ordering;

-- With ASC as default, creating a table in a tablegroup with SPLIT AT should fail
CREATE TABLE split_fail(a int primary key, b int)
  SPLIT AT VALUES ((1000),(2000),(3000))
  TABLEGROUP postgres_ordering;

CREATE TABLE like_hash_table (like hash_table including all) TABLEGROUP postgres_ordering;

SET yb_use_hash_splitting_by_default = false;

CREATE TABLE like_hash_table (like hash_table including all) TABLEGROUP postgres_ordering;

DROP FUNCTION get_table_indexes(text);
