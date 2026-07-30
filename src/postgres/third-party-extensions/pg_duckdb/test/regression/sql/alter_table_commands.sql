-- Test all ALTER TABLE commands supported by pgduckdb_get_alterdef
-- Set up a test table
SET duckdb.force_execution = false;
CREATE TEMP TABLE alter_test(
    id INT,
    name TEXT,
    value DOUBLE PRECISION DEFAULT 0.0,
    created_at TIMESTAMP
) USING duckdb;

INSERT INTO alter_test VALUES (1, 'test1', 10.5, '2023-01-01 12:00:00');
INSERT INTO alter_test VALUES (2, 'test2', 20.5, '2023-01-02 12:00:00');

-- Verify initial state
SELECT * FROM alter_test ORDER BY id;
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test');

-- ADD COLUMN
ALTER TABLE alter_test ADD COLUMN description TEXT;
ALTER TABLE alter_test ADD COLUMN active BOOLEAN DEFAULT true;
ALTER TABLE alter_test ADD COLUMN score INT DEFAULT 100 NULL;
ALTER TABLE alter_test ADD COLUMN score2 INT DEFAULT 100 NOT NULL;
ALTER TABLE alter_test ADD COLUMN score3 INT DEFAULT 100 CHECK (score3 >= score);
ALTER TABLE alter_test ADD COLUMN id2 INT PRIMARY KEY;
ALTER TABLE alter_test ADD COLUMN id3 INT UNIQUE;
ALTER TABLE alter_test ADD COLUMN german text COLLATE "de-x-icu";

-- Verify columns were added
SELECT * FROM alter_test ORDER BY id;
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test');

-- ALTER COLUMN TYPE
ALTER TABLE alter_test ALTER COLUMN id TYPE BIGINT;
ALTER TABLE alter_test ALTER COLUMN value TYPE REAL;

-- Verify column types were changed
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test');

-- DROP COLUMN
ALTER TABLE alter_test DROP COLUMN description;

-- Verify column was dropped
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test');

-- SET/DROP DEFAULT
ALTER TABLE alter_test ALTER COLUMN name SET DEFAULT 'unnamed';
INSERT INTO alter_test(id) VALUES (3);
SELECT * FROM alter_test ORDER BY id;

ALTER TABLE alter_test ALTER COLUMN name DROP DEFAULT;
INSERT INTO alter_test(id) VALUES (4);
SELECT * FROM alter_test ORDER BY id;

-- Delete this one before adding the NOT NULL constraint
DELETE FROM alter_test WHERE id = 4;

-- SET/DROP NOT NULL
ALTER TABLE alter_test ALTER COLUMN name SET NOT NULL;
-- This should fail
UPDATE alter_test SET name = NULL WHERE id = 1;

ALTER TABLE alter_test ALTER COLUMN name DROP NOT NULL;
-- This should succeed
UPDATE alter_test SET name = NULL WHERE id = 1;
SELECT * FROM alter_test WHERE id = 1;

-- ADD CONSTRAINT (CHECK)
ALTER TABLE alter_test ADD CONSTRAINT positive_id CHECK (id > 0);
-- This should fail
INSERT INTO alter_test(id, name) VALUES (-1, 'negative');

-- ADD CONSTRAINT (PRIMARY KEY)
ALTER TABLE alter_test ADD PRIMARY KEY (id);
-- This should fail due to duplicate key
INSERT INTO alter_test(id, name) VALUES (1, 'duplicate');

-- ADD CONSTRAINT (UNIQUE)
ALTER TABLE alter_test ADD CONSTRAINT unique_name UNIQUE (name);
-- This should fail due to duplicate name
UPDATE alter_test SET name = 'test2' WHERE id = 3;

-- DROP CONSTRAINT
ALTER TABLE alter_test DROP CONSTRAINT unique_name;
-- This should now succeed
UPDATE alter_test SET name = 'test2' WHERE id = 3;
SELECT * FROM alter_test WHERE id = 3;

ALTER TABLE alter_test DROP CONSTRAINT positive_id;
-- This should now succeed
INSERT INTO alter_test(id, name) VALUES (-1, 'negative');
SELECT * FROM alter_test WHERE id = -1;

-- SET/RESET table options
-- Note: DuckDB supports limited table options compared to PostgreSQL
ALTER TABLE alter_test SET (fillfactor = 90);
ALTER TABLE alter_test RESET (fillfactor);

ALTER TABLE alter_test RENAME TO alter_test2;
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test2');
SELECT * from alter_test2 ORDER BY id;

ALTER TABLE alter_test2 RENAME COLUMN active TO active2;
SELECT * FROM duckdb.query('DESCRIBE pg_temp.alter_test2');
SELECT * from alter_test2 ORDER BY id;

-- Clean up
DROP TABLE alter_test2;

