CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3)) SPLIT INTO 1 TABLETS;
CREATE INDEX ON items USING ybdummyann (embedding vector_l2_ops);
INSERT INTO items VALUES (1, '[1.0, 0.4, 0.3]');
INSERT INTO items VALUES (2, '[0.001, 0.432, 0.32]');
\d items

EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;
SELECT * FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;

EXPLAIN (COSTS OFF) SELECT embedding FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;
SELECT embedding FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;

DELETE FROM items WHERE id = 1;

SELECT * FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;

DROP TABLE items;

CREATE TABLE itemmultitablets (id serial PRIMARY KEY, embedding vector(3)) SPLIT INTO 2 TABLETS;
-- Should not work on a table with multiple tablets.
CREATE INDEX ON items USING ybhnsw (embedding vector_l2_ops);

-- Create a table with a vector column of size 10.
CREATE TABLE items (id serial PRIMARY KEY, embedding vector(10)) SPLIT INTO 1 TABLETS;

-- Does not order results across tablets yet.
CREATE INDEX ON items USING ybhnsw (embedding vector_l2_ops);

-- Insert 30 rows of sample data with embeddings having float values between 0 and 1.
INSERT INTO items (embedding) VALUES
-- Base vector and very close neighbors (unique tiny differences)
('[1,1,1,1,1,1,1,1,1,1]'),
('[1,1,1,1,1,1,1,1,1,1.1]'),
('[1,1,1,1,1,1,1,1,1,1.3]'),
('[1,1,1,1,1,1,1,1,1,1.5]'),
-- Single dimension variations
('[1,1,1,1,1,1,1,1.7,1,1]'),
('[1,1,1,1,1,1.9,1,1,1,1]'),
('[1,1,1,2.1,1,1,1,1,1,1]'),
('[2.3,1,1,1,1,1,1,1,1,1]'),
-- Two dimension variations
('[2,2.5,1,1,1,1,1,1,1,1]'),
('[1,1,2.7,2.8,1,1,1,1,1,1]'),
('[1,1,1,1,2.9,3,1,1,1,1]'),
('[1,1,1,1,1,1,3.1,3.2,1,1]'),
-- Three dimension variations
('[3.3,3.4,3.5,1,1,1,1,1,1,1]'),
('[1,1,1,3.6,3.7,3.8,1,1,1,1]'),
('[1,1,1,1,1,1,3.9,4,4.1,1]'),
-- Larger differences in various positions
('[4.2,1,1,1,1,1,1,1,1,1]'),
('[1,4.4,1,1,1,1,1,1,1,1]'),
('[1,1,4.6,1,1,1,1,1,1,1]'),
('[1,1,1,4.8,1,1,1,1,1,1]'),
-- Multiple large differences
('[5,5.1,1,1,1,1,1,1,1,1]'),
('[1,1,5.2,5.3,1,1,1,1,1,1]'),
('[1,1,1,1,5.4,5.5,1,1,1,1]'),
('[1,1,1,1,1,1,5.6,5.7,1,1]'),
('[1,1,1,1,1,1,1,1,5.8,5.9]'),
-- Extreme outliers
('[6,1,1,1,1,1,1,1,1,1]'),
('[7,7.1,1,1,1,1,1,1,1,1]'),
('[8,8.1,8.2,1,1,1,1,1,1,1]'),
('[1,1,1,1,1,1,1,1,9,9.1]'),
('[10,10,10,10,10,1,1,1,1,1]');

-- Reference query.
/*+SeqScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';
/*+SeqScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';

/*+IndexScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';
/*+IndexScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';

-- Reference query.
/*+SeqScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[10,10,10,10,10,1,1,1,1,1]';
/*+SeqScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[10,10,10,10,10,1,1,1,1,1]';

/*+IndexScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[10,10,10,10,10,1,1,1,1,1]';
/*+IndexScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[10,10,10,10,10,1,1,1,1,1]';

-- Wrong dimensionality, shouldn't work.
SELECT * FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;

DROP INDEX items_embedding_idx;

-- Dummy implementation, should only provide Exact ANN within a tablet.
CREATE INDEX ON items USING ybdummyann (embedding vector_l2_ops);
EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';
SELECT * FROM items ORDER BY embedding <-> '[1,1,1,1,1,1,1,1,1,1]';

DROP TABLE items;
