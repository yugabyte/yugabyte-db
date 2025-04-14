-- Test primary key column order differs table creation column order --
CREATE TABLE st_keys(k4 int, k3 int, k2 int, k1 int, value int, PRIMARY KEY(k1, k2, k3, k4));
CREATE INDEX ON st_keys(k4);
CREATE UNIQUE INDEX ON st_keys(value);
INSERT INTO st_keys VALUES(4, 3, 2, 1, 100), (40, 30, 20, 10, 200), (400, 300, 200, 100, 300);
SELECT * FROM st_keys ORDER BY k1;
EXPLAIN(COSTS OFF) SELECT * FROM st_keys WHERE value = 200 ORDER BY k1;
SELECT * FROM st_keys WHERE value = 200 ORDER BY k1;
EXPLAIN(COSTS OFF) SELECT * FROM st_keys WHERE k4 = 400 ORDER BY k1;
SELECT * FROM st_keys WHERE k4 = 400 ORDER BY k1;

UPDATE st_keys SET value=0 WHERE k2 = 20;
SELECT k1, k2, k3, k4, value FROM st_keys ORDER BY k1;
DELETE FROM st_keys WHERE k4 = 400;
SELECT value, k4, k3, k2, k1 FROM st_keys ORDER BY k1;

CREATE TABLE dt_keys(k3 int, k2 text, k1 int, PRIMARY KEY(k1, k2, k3));
INSERT INTO dt_keys VALUES(3, 'value_2', 1);
INSERT INTO dt_keys(k1, k2, k3) VALUES(10, 'value_20', 30);
SELECT * FROM dt_keys ORDER BY k1;
SELECT k1, k2, k3 FROM dt_keys ORDER BY k1;
