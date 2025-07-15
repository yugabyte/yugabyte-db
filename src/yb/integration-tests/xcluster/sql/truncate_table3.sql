--
-- Part 3 of TRUNCATE_TABLE test
-- Inserts new data after truncation
--

-- Simple table
INSERT INTO tbl1 VALUES (1, 'a'), (2, 'b'), (3, 'c');

-- Partitioned table
INSERT INTO tbl2 VALUES (1, 'a'), (2, 'b'), (3, 'c'),
                       (4, 'd'), (5, 'e'), (6, 'f');

-- Tables with indexes
INSERT INTO tbl3 VALUES (1, 'a'), (2, 'b'), (3, 'c');

-- Tables with foreign keys
INSERT INTO tbl4 VALUES (11, 'a'), (12, 'b'), (13, 'c');
INSERT INTO tbl4_fk VALUES (21, 11), (22, 12), (23, 13);
INSERT INTO tbl4_fk_fk VALUES (1, 21), (2, 22), (3, 23);

-- Table with a sequence
INSERT INTO tbl5 (data) VALUES ('a'), ('b'), ('c');

-- Inherited table
INSERT INTO cities VALUES
  ('New York', 8175133, 33);
INSERT INTO capitals VALUES
  ('Albany', 98756, 42, 'NY');

INSERT INTO cities2 VALUES
  ('Philadelphia', 1584200, 39);
INSERT INTO capitals2 VALUES
  ('Harrisburg', 49528, 370, 'PA');
