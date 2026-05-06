CREATE TABLE tbl (id int, c float8, d text);
INSERT INTO tbl SELECT i, i * 2.0, 'helloworld' FROM generate_series(1, 1000000) i;
SELECT * FROM tbl ORDER BY 1,2,3 LIMIT 3;
DROP TABLE tbl;

-- JSON/LIST type
CREATE TABLE tbl (id int, c jsonb, d text[]);
INSERT INTO tbl SELECT i, jsonb_build_object('a', i), array_agg(i) FROM generate_series(1, 500000) i GROUP BY i;
SELECT * FROM tbl ORDER BY id DESC LIMIT 3;

CREATE TABLE tbl1 (id int, c jsonb, d text[]);
INSERT INTO tbl1 SELECT i, jsonb_build_object('a', i), array_agg(i) FROM generate_series(1, 100000) i GROUP BY i;
SELECT tbl.id, tbl.c, tbl.d, tbl1.c, tbl1.d FROM tbl JOIN tbl1 ON tbl.id = tbl1.id ORDER BY 1,2,3,4 LIMIT 5;

DROP TABLE tbl, tbl1;

