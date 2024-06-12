CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3));
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
