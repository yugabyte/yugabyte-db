--
-- PATH
--

--DROP TABLE PATH_TBL;

CREATE TABLE PATH_TBL (id int primary key, f1 path);

INSERT INTO PATH_TBL VALUES (1, '[(1,2),(3,4)]');

INSERT INTO PATH_TBL VALUES (2, '((1,2),(3,4))');

INSERT INTO PATH_TBL VALUES (3, '[(0,0),(3,0),(4,5),(1,6)]');

INSERT INTO PATH_TBL VALUES (4, '((1,2),(3,4))');

INSERT INTO PATH_TBL VALUES (5, '1,2 ,3,4');

INSERT INTO PATH_TBL VALUES (6, '[1,2,3, 4]');

INSERT INTO PATH_TBL VALUES (7, '[11,12,13,14]');

INSERT INTO PATH_TBL VALUES (8, '(11,12,13,14)');

-- bad values for parser testing
INSERT INTO PATH_TBL VALUES (100, '[(,2),(3,4)]');

INSERT INTO PATH_TBL VALUES (101, '[(1,2),(3,4)');

SELECT f1 FROM PATH_TBL ORDER BY id;

SELECT '' AS count, f1 AS open_path FROM PATH_TBL WHERE isopen(f1) ORDER BY id;

SELECT '' AS count, f1 AS closed_path FROM PATH_TBL WHERE isclosed(f1) ORDER BY id;

SELECT '' AS count, pclose(f1) AS closed_path FROM PATH_TBL ORDER BY id;

SELECT '' AS count, popen(f1) AS open_path FROM PATH_TBL ORDER BY id;
