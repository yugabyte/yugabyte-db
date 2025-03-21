CREATE TABLE num_data (id int4, val numeric(210,10));

BEGIN TRANSACTION;
INSERT INTO num_data VALUES (0, '0');
INSERT INTO num_data VALUES (1, '0');
INSERT INTO num_data VALUES (2, '-34338492.215397047');
INSERT INTO num_data VALUES (3, '4.31');
INSERT INTO num_data VALUES (4, '7799461.4119');
INSERT INTO num_data VALUES (5, '16397.038491');
INSERT INTO num_data VALUES (6, '93901.57763026');
INSERT INTO num_data VALUES (7, '-83028485');
INSERT INTO num_data VALUES (8, '74881');
INSERT INTO num_data VALUES (9, '-24926804.045047420');
COMMIT TRANSACTION;

-- ASC/DESC check
SELECT * FROM num_data ORDER BY val ASC, id ASC;
SELECT * FROM num_data ORDER BY val DESC, id DESC;

--
-- Test for PRIMARY KEY
--

CREATE TABLE num_data_with_pk(id NUMERIC PRIMARY KEY, val numeric);
INSERT INTO num_data_with_pk VALUES ('1.1','-11.11');
INSERT INTO num_data_with_pk VALUES ('2.2','-22.22');
INSERT INTO num_data_with_pk VALUES ('3.3','-33.33');
SELECT * FROM num_data_with_pk ORDER BY id;
SELECT VAL FROM num_data_with_pk WHERE id = '2.2';
