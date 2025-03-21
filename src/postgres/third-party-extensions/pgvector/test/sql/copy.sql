-- vector

CREATE TABLE t (val vector(3));
INSERT INTO t (val) VALUES ('[0,0,0]'), ('[1,2,3]'), ('[1,1,1]'), (NULL);

CREATE TABLE t2 (val vector(3));

\copy t TO 'results/vector.bin' WITH (FORMAT binary)
\copy t2 FROM 'results/vector.bin' WITH (FORMAT binary)

SELECT * FROM t2 ORDER BY val;

DROP TABLE t;
DROP TABLE t2;

-- halfvec

CREATE TABLE t (val halfvec(3));
INSERT INTO t (val) VALUES ('[0,0,0]'), ('[1,2,3]'), ('[1,1,1]'), (NULL);

CREATE TABLE t2 (val halfvec(3));

\copy t TO 'results/halfvec.bin' WITH (FORMAT binary)
\copy t2 FROM 'results/halfvec.bin' WITH (FORMAT binary)

SELECT * FROM t2 ORDER BY val;

DROP TABLE t;
DROP TABLE t2;

-- sparsevec

CREATE TABLE t (val sparsevec(3));
INSERT INTO t (val) VALUES ('{}/3'), ('{1:1,2:2,3:3}/3'), ('{1:1,2:1,3:1}/3'), (NULL);

CREATE TABLE t2 (val sparsevec(3));

\copy t TO 'results/sparsevec.bin' WITH (FORMAT binary)
\copy t2 FROM 'results/sparsevec.bin' WITH (FORMAT binary)

SELECT * FROM t2 ORDER BY val;

DROP TABLE t;
DROP TABLE t2;
