--
-- CREATE SEQUENCE
--

--- test creation of SERIAL column

CREATE TABLE serialTest1 (f1 text, f2 serial);

SELECT * FROM serialTest1_f2_seq;

INSERT INTO serialTest1 VALUES ('foo');
INSERT INTO serialTest1 VALUES ('bar');
INSERT INTO serialTest1 VALUES ('force', 100);
INSERT INTO serialTest1 VALUES ('wrong', NULL);

SELECT * FROM serialTest1 ORDER BY f1;

SELECT * FROM serialTest1_f2_seq;

SELECT pg_get_serial_sequence('serialTest1', 'f2');



-- similar to above but on TEMPORARY TABLE

CREATE TEMPORARY TABLE serialTest2 (id serial, val int);

INSERT INTO serialTest2 (val) VALUES (95);
INSERT INTO serialTest2 (val) VALUES (-3);
INSERT INTO serialTest2 (val) VALUES (18);
INSERT INTO serialTest2 (id, val) VALUES (1, 19);

SELECT * FROM serialTest2;

SELECT * FROM serialTest2_id_seq;

SELECT setval('serialTest2_id_seq', 99, true);
INSERT INTO serialTest2 (val) VALUES (40);
INSERT INTO serialTest2 (val) VALUES (41);
INSERT INTO serialTest2 (val) VALUES (42);
SELECT * FROM serialTest2;

DROP TABLE serialTest2;


-- operations directly on sequences

CREATE SEQUENCE sequence_test1;

SELECT * from sequence_test1;

SELECT nextval('sequence_test1');
SELECT nextval('sequence_test1');
SELECT nextval('sequence_test1');

SELECT last_value FROM sequence_test1;

SELECT nextval('sequence_test1');
SELECT lastval();

SELECT setval('sequence_test1', 50);
SELECT nextval('sequence_test1');
SELECT nextval('sequence_test1');

SELECT setval('sequence_test1', 50, false);
SELECT nextval('sequence_test1');



-- similar to above but on TEMPORARY SEQUENCE

CREATE TEMPORARY SEQUENCE sequence_test2;

SELECT nextval('sequence_test2');
SELECT nextval('sequence_test2');
SELECT nextval('sequence_test2');
SELECT nextval('sequence_test2');

SELECT * FROM sequence_test2;

DROP SEQUENCE sequence_test2;

-- Information schema

SELECT * FROM information_schema.sequences
  WHERE sequence_name ~ ANY(ARRAY['sequence_test', 'serialtest'])
  ORDER BY sequence_name ASC;

SELECT schemaname, sequencename, start_value, min_value, max_value, increment_by, cycle, cache_size, last_value
FROM pg_sequences
WHERE sequencename ~ ANY(ARRAY['sequence_test', 'serialtest'])
  ORDER BY sequencename ASC;
