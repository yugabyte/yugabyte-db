--
-- Part 1 of TRUNCATE_TABLE test
-- Creates necessary tables and inserts initial data
--

-- Simple table
CREATE TABLE tbl1 (id int, data text);
INSERT INTO tbl1 VALUES (1, 'one'), (2, 'two'), (3, 'three');

-- Partitioned table
CREATE TABLE tbl2 (id int, data text) PARTITION BY RANGE (id);
CREATE TABLE tbl2_p1 PARTITION OF tbl2 FOR VALUES FROM (1) TO (4);
CREATE TABLE tbl2_p2 PARTITION OF tbl2 FOR VALUES FROM (4) TO (11);
INSERT INTO tbl2 VALUES (1, 'one'), (2, 'two'), (3, 'three'),
                       (4, 'four'), (5, 'five'), (6, 'six');

-- Tables with indexes
CREATE TABLE tbl3 (id int, data text);
CREATE INDEX tbl3_idx1 ON tbl3 (data);
CREATE INDEX tbl3_idx2 ON tbl3 (id);
INSERT INTO tbl3 VALUES (1, 'one'), (2, 'two'), (3, 'three');

-- Tables with foreign keys
CREATE TABLE tbl4 (id int PRIMARY KEY, data text);
CREATE TABLE tbl4_fk (id int PRIMARY KEY, tbl4_id int REFERENCES tbl4(id));
CREATE TABLE tbl4_fk_fk (id int, tbl4_fk_id int REFERENCES tbl4_fk(id));
INSERT INTO tbl4 VALUES (1, 'one'), (2, 'two'), (3, 'three');
INSERT INTO tbl4_fk VALUES (1, 1), (2, 2), (3, 3);
INSERT INTO tbl4_fk_fk VALUES (1, 1), (2, 2), (3, 3);

-- Table with a sequence
CREATE TABLE tbl5(id SERIAL PRIMARY KEY, data text);
INSERT INTO tbl5 (data) VALUES ('one'), ('two'), ('three');

-- Inherited table
CREATE TABLE cities (
  name       text,
  population real,
  elevation  int     -- (in ft)
);
INSERT INTO cities VALUES
  ('San Francisco', 864816, 16),
  ('Los Angeles', 3971883, 305);

CREATE TABLE capitals (
  state      char(2) UNIQUE NOT NULL
) INHERITS (cities);
INSERT INTO capitals VALUES
  ('Sacramento', 10393, 57, 'CA'),
  ('Austin', 97854, 489, 'TX');

CREATE TABLE cities2 (
  name       text,
  population real,
  elevation  int     -- (in ft)
);
INSERT INTO cities2 VALUES
  ('Chicago', 2716000, 594),
  ('Houston', 2328000, 43);
CREATE TABLE capitals2 (
  state      char(2) UNIQUE NOT NULL
) INHERITS (cities2);
INSERT INTO capitals2 VALUES
  ('Springfield', 114394, 597, 'IL'),
  ('Denver', 715522, 5280, 'CO');
