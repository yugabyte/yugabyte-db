SET search_path TO public;

CREATE EXTENSION btree_gist;
CREATE EXTENSION btree_gin;

CREATE ROLE super_user
  SUPERUSER
  NOCREATEDB
  NOCREATEROLE
  NOINHERIT
  NOLOGIN
  NOREPLICATION
  CONNECTION LIMIT 1;
CREATE ROLE normal_user
  NOSUPERUSER
  NOCREATEDB
  NOCREATEROLE
  NOINHERIT
  NOLOGIN
  NOREPLICATION
  CONNECTION LIMIT 1;

DROP SCHEMA s1 CASCADE;
CREATE SCHEMA s1;
CREATE SCHEMA s2;

CREATE TABLE s1.t1 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.t2 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.t3 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s2.t1 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.p1 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.p2 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.p1c1 (LIKE s1.p1 INCLUDING ALL, CHECK (c1 <= 100)) INHERITS(s1.p1);
CREATE TABLE s1.p1c2 (LIKE s1.p1 INCLUDING ALL, CHECK (c1 > 100 AND c1 <= 200)) INHERITS(s1.p1);
CREATE TABLE s1.p1c3 (LIKE s1.p1 INCLUDING ALL, CHECK (c1 > 200)) INHERITS(s1.p1);
CREATE TABLE s1.p2c1 (LIKE s1.p2 INCLUDING ALL, CHECK (c1 <= 100)) INHERITS(s1.p2);
CREATE TABLE s1.p2c2 (LIKE s1.p2 INCLUDING ALL, CHECK (c1 > 100 AND c1 <= 200)) INHERITS(s1.p2);
CREATE TABLE s1.p2c3 (LIKE s1.p2 INCLUDING ALL, CHECK (c1 > 200)) INHERITS(s1.p2);
CREATE TABLE s1.p2c1c1 (LIKE s1.p2c1 INCLUDING ALL, CHECK (c1 <= 50)) INHERITS(s1.p2c1);
CREATE TABLE s1.p2c1c2 (LIKE s1.p2c1 INCLUDING ALL, CHECK (c1 > 50 AND c1 <= 100)) INHERITS(s1.p2c1);
CREATE TABLE s1.p2c2c1 (LIKE s1.p2c2 INCLUDING ALL, CHECK (c1 > 100 AND c1 <= 150)) INHERITS(s1.p2c2);
CREATE TABLE s1.p2c2c2 (LIKE s1.p2c2 INCLUDING ALL, CHECK (c1 > 150 AND c1 <= 200)) INHERITS(s1.p2c2);
CREATE TABLE s1.p2c3c1 (LIKE s1.p2c3 INCLUDING ALL, CHECK (c1 > 200 AND c1 <= 250)) INHERITS(s1.p2c3);
CREATE TABLE s1.p2c3c2 (LIKE s1.p2c3 INCLUDING ALL, CHECK (c1 > 250)) INHERITS(s1.p2c3);
CREATE TABLE s1.r1 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.r2 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.r3 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1));
CREATE TABLE s1.ti1 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1), UNIQUE (c2));
CREATE UNLOGGED TABLE s1.ul1 (LIKE s1.t1 INCLUDING ALL);

INSERT INTO s1.t1 SELECT i, i, i % 100, i FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO s1.t2 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.t3 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s2.t1 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.p1c1 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.p1c2 SELECT i, i, i % 10, i FROM (SELECT generate_series(101, 200) i) t;
INSERT INTO s1.p1c3 SELECT i, i, i % 10, i FROM (SELECT generate_series(201, 300) i) t;
INSERT INTO s1.p2c1c1 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 50) i) t;
INSERT INTO s1.p2c1c2 SELECT i, i, i % 10, i FROM (SELECT generate_series(51, 100) i) t;
INSERT INTO s1.p2c2c1 SELECT i, i, i % 10, i FROM (SELECT generate_series(101, 150) i) t;
INSERT INTO s1.p2c2c2 SELECT i, i, i % 10, i FROM (SELECT generate_series(151, 200) i) t;
INSERT INTO s1.p2c3c1 SELECT i, i, i % 10, i FROM (SELECT generate_series(201, 250) i) t;
INSERT INTO s1.p2c3c2 SELECT i, i, i % 10, i FROM (SELECT generate_series(251, 300) i) t;
INSERT INTO s1.r1 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.r2 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.r3 SELECT i, i, i % 10, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO s1.ti1 SELECT i, i, i % 100, i FROM (SELECT generate_series(1, 1000) i) t;

CREATE INDEX t1_i ON s1.t1 (c3);
CREATE INDEX t2_i ON s1.t2 (c2);
CREATE INDEX t3_i ON s1.t3 (c2);
CREATE INDEX t1_i ON s2.t1 (c2);
CREATE INDEX p1_i ON s1.p1 (c2);
CREATE INDEX p2_i ON s1.p2 (c2);
CREATE INDEX p1c1_i ON s1.p1c1 (c1);
CREATE INDEX p1c2_i ON s1.p1c2 (c1);
CREATE INDEX p1c3_i ON s1.p1c3 (c1);
CREATE INDEX p2c1_i ON s1.p2c1 (c1);
CREATE INDEX p2c2_i ON s1.p2c2 (c1);
CREATE INDEX p2c3_i ON s1.p2c3 (c1);
CREATE INDEX p2c1c1_i ON s1.p2c1c1 (c1);
CREATE INDEX p2c1c2_i ON s1.p2c1c2 (c1);
CREATE INDEX p2c2c1_i ON s1.p2c2c1 (c1);
CREATE INDEX p2c2c2_i ON s1.p2c2c2 (c1);
CREATE INDEX p2c3c1_i ON s1.p2c3c1 (c1);
CREATE INDEX p2c3c2_i ON s1.p2c3c2 (c1);
CREATE INDEX r1_i ON s1.r1 (c2);
CREATE INDEX r2_i ON s1.r2 (c2);
CREATE INDEX r3_i ON s1.r3 (c2);
CREATE INDEX ti1_i1    ON s1.ti1 (c2);
CREATE INDEX ti1_i2    ON s1.ti1 (c2, c4);
CREATE INDEX ti1_i3    ON s1.ti1 (c2, c4, c4);
CREATE INDEX ti1_i4    ON s1.ti1 (c2, c4, c4, c4);
CREATE INDEX ti1_btree ON s1.ti1 USING btree (c1);
CREATE INDEX ti1_hash  ON s1.ti1 USING hash (c1);
CREATE INDEX ti1_gist  ON s1.ti1 USING gist (c1);
CREATE INDEX ti1_gin   ON s1.ti1 USING gin (c1);
CREATE INDEX ti1_expr  ON s1.ti1 ((c1 < 100));
CREATE INDEX ti1_pred  ON s1.ti1 (lower(c4));
CREATE UNIQUE INDEX ti1_uniq ON s1.ti1 (c1);
CREATE INDEX ti1_multi ON s1.ti1 (c1, c2, c3, c4);
CREATE INDEX ti1_ts    ON s1.ti1 USING gin(to_tsvector('english', c4));

CREATE VIEW s1.v1 AS SELECT v1t1.c1, v1t1.c2, v1t1.c3, v1t1.c4 FROM s1.t1 v1t1;
CREATE VIEW s1.v1_ AS SELECT v1t1_.c1, v1t1_.c2, v1t1_.c3, v1t1_.c4 FROM s1.t1 v1t1_;
CREATE VIEW s1.v2 AS SELECT v2t1.c1, v2t1.c2, v2t1.c3, v2t1.c4 FROM s1.t1 v2t1 JOIN s1.t2 v2t2 ON(v2t1.c1 = v2t2.c1);
CREATE VIEW s1.v3 AS SELECT v3t1.c1, v3t1.c2, v3t1.c3, v3t1.c4 FROM s1.t1 v3t1 JOIN s1.t2 v3t2 ON(v3t1.c1 = v3t2.c1) JOIN s1.t3 v3t3 ON(v3t1.c1 = v3t3.c1);

ANALYZE;

CREATE FUNCTION s1.f1 () RETURNS s1.t1 AS $$
VALUES(1,1,1,'1'), (2,2,2,'2'), (3,3,3,'3')
$$ LANGUAGE sql;

\q
CREATE RULE r1_s_a AS ON SELECT TO s1.r1 WHERE true DO ALSO SELECT * FROM t1;
CREATE RULE r1_s_i AS ON SELECT TO s1.r1 WHERE true DO INSTEAD SELECT * FROM t1;

