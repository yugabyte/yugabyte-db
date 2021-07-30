--
-- Yugabyte-owned test for ybgin access method.
--

--
-- Create non-temp table which uses Yugabyte storage.
--
CREATE TABLE vectors (i serial PRIMARY KEY, v tsvector);

--
-- tsvector
--
INSERT INTO vectors (v) VALUES
    (to_tsvector('simple', 'a bb ccc')),
    (to_tsvector('simple', 'bb a e i o u')),
    (to_tsvector('simple', 'ccd'));
CREATE INDEX ON vectors USING ybgin (v);

-- Cleanup
DROP TABLE vectors;

--
-- Try creating ybgin index on temp table.
--
CREATE TEMP TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE INDEX ON vectors USING ybgin (v);

-- Cleanup
DISCARD TEMP;
