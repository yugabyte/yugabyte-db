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

-- Create a table with a vector column of size 10
CREATE TABLE items (id serial PRIMARY KEY, embedding vector(10));

CREATE INDEX ON items USING ybdummyann (embedding vector_l2_ops);

-- Insert 30 rows of sample data with embeddings having float values between 0 and 1
INSERT INTO items (embedding) VALUES
('[0.12, 0.78, 0.56, 0.34, 0.91, 0.29, 0.47, 0.68, 0.22, 0.85]'),
('[0.33, 0.14, 0.79, 0.58, 0.07, 0.42, 0.91, 0.27, 0.36, 0.84]'),
('[0.45, 0.64, 0.27, 0.19, 0.53, 0.48, 0.91, 0.85, 0.14, 0.72]'),
('[0.24, 0.85, 0.61, 0.32, 0.78, 0.45, 0.27, 0.56, 0.11, 0.90]'),
('[0.57, 0.13, 0.84, 0.46, 0.95, 0.22, 0.38, 0.73, 0.19, 0.67]'),
('[0.67, 0.24, 0.51, 0.78, 0.32, 0.81, 0.49, 0.15, 0.93, 0.56]'),
('[0.89, 0.36, 0.42, 0.71, 0.57, 0.18, 0.64, 0.28, 0.95, 0.37]'),
('[0.72, 0.58, 0.13, 0.49, 0.81, 0.64, 0.23, 0.45, 0.76, 0.12]'),
('[0.13, 0.97, 0.42, 0.81, 0.65, 0.24, 0.56, 0.38, 0.72, 0.48]'),
('[0.55, 0.72, 0.19, 0.83, 0.31, 0.48, 0.67, 0.59, 0.11, 0.90]'),
('[0.48, 0.29, 0.78, 0.52, 0.61, 0.14, 0.93, 0.37, 0.85, 0.26]'),
('[0.21, 0.43, 0.56, 0.72, 0.49, 0.64, 0.31, 0.85, 0.12, 0.78]'),
('[0.84, 0.11, 0.97, 0.46, 0.35, 0.58, 0.73, 0.92, 0.15, 0.67]'),
('[0.37, 0.52, 0.74, 0.18, 0.69, 0.43, 0.27, 0.81, 0.56, 0.12]'),
('[0.61, 0.28, 0.45, 0.72, 0.13, 0.89, 0.38, 0.57, 0.94, 0.26]'),
('[0.93, 0.14, 0.58, 0.72, 0.35, 0.49, 0.18, 0.67, 0.28, 0.82]'),
('[0.42, 0.78, 0.56, 0.34, 0.89, 0.71, 0.25, 0.63, 0.19, 0.52]'),
('[0.64, 0.19, 0.58, 0.73, 0.11, 0.46, 0.29, 0.85, 0.92, 0.35]'),
('[0.51, 0.22, 0.67, 0.34, 0.78, 0.91, 0.42, 0.56, 0.19, 0.85]'),
('[0.29, 0.67, 0.41, 0.73, 0.15, 0.52, 0.89, 0.36, 0.48, 0.94]'),
('[0.83, 0.56, 0.19, 0.41, 0.72, 0.27, 0.49, 0.93, 0.11, 0.64]'),
('[0.47, 0.22, 0.79, 0.34, 0.55, 0.18, 0.83, 0.71, 0.92, 0.13]'),
('[0.14, 0.85, 0.67, 0.41, 0.58, 0.92, 0.19, 0.73, 0.35, 0.48]'),
('[0.63, 0.24, 0.57, 0.91, 0.36, 0.42, 0.19, 0.78, 0.53, 0.85]'),
('[0.27, 0.82, 0.13, 0.69, 0.46, 0.94, 0.31, 0.58, 0.71, 0.25]'),
('[0.57, 0.38, 0.49, 0.74, 0.19, 0.91, 0.32, 0.85, 0.14, 0.63]'),
('[0.18, 0.59, 0.93, 0.27, 0.64, 0.78, 0.46, 0.52, 0.11, 0.85]'),
('[0.91, 0.22, 0.36, 0.48, 0.74, 0.67, 0.19, 0.56, 0.35, 0.81]'),
('[0.42, 0.78, 0.35, 0.67, 0.53, 0.91, 0.18, 0.72, 0.11, 0.29]'),
('[0.61, 0.57, 0.13, 0.46, 0.84, 0.92, 0.36, 0.58, 0.71, 0.25]');

EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[0.64, 0.19, 0.58, 0.73, 0.11, 0.46, 0.29, 0.85, 0.92, 0.35]';
SELECT * FROM items ORDER BY embedding <-> '[0.64, 0.19, 0.58, 0.73, 0.11, 0.46, 0.29, 0.85, 0.92, 0.35]';

/*SeqScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[0.64, 0.19, 0.58, 0.73, 0.11, 0.46, 0.29, 0.85, 0.92, 0.35]';
/*SeqScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[0.64, 0.19, 0.58, 0.73, 0.11, 0.46, 0.29, 0.85, 0.92, 0.35]';

EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[0.53, 0.38, 0.49, 0.74, 0.19, 0.91, 0.32, 0.85, 0.14, 0.63]';
SELECT * FROM items ORDER BY embedding <-> '[0.53, 0.38, 0.49, 0.74, 0.19, 0.91, 0.32, 0.85, 0.14, 0.63]';

/*SeqScan(items)*/ EXPLAIN (COSTS OFF) SELECT * FROM items ORDER BY embedding <-> '[0.53, 0.38, 0.49, 0.74, 0.19, 0.91, 0.32, 0.85, 0.14, 0.63]';
/*SeqScan(items)*/ SELECT * FROM items ORDER BY embedding <-> '[0.53, 0.38, 0.49, 0.74, 0.19, 0.91, 0.32, 0.85, 0.14, 0.63]';

-- Wrong dimensionality, shouldn't work.
SELECT * FROM items ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5;

DROP TABLE items;
