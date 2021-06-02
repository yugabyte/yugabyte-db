--
-- LSEG
-- Line segments
--

--DROP TABLE LSEG_TBL;
CREATE TABLE LSEG_TBL (id int primary key, s lseg);

INSERT INTO LSEG_TBL VALUES (1, '[(1,2),(3,4)]');
INSERT INTO LSEG_TBL VALUES (2, '(0,0),(6,6)');
INSERT INTO LSEG_TBL VALUES (3, '10,-10 ,-3,-4');
INSERT INTO LSEG_TBL VALUES (4, '[-1e6,2e2,3e5, -4e1]');
INSERT INTO LSEG_TBL VALUES (5, '(11,22,33,44)');

-- bad values for parser testing
INSERT INTO LSEG_TBL VALUES (6, '(3asdf,2 ,3,4r2)');
INSERT INTO LSEG_TBL VALUES (7, '[1,2,3, 4');
INSERT INTO LSEG_TBL VALUES (8, '[(,2),(3,4)]');
INSERT INTO LSEG_TBL VALUES (9, '[(1,2),(3,4)');

select * from LSEG_TBL ORDER BY id;

SELECT * FROM LSEG_TBL WHERE s <= lseg '[(1,2),(3,4)]' ORDER BY id;

SELECT * FROM LSEG_TBL WHERE (s <-> lseg '[(1,2),(3,4)]') < 10 ORDER BY id;
