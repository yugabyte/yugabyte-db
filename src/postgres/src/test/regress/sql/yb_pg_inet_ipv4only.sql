--
-- INET (IPv4 addresses only)
--

-- prepare the table...

DROP TABLE IF EXISTS INET_TBL;
CREATE TABLE INET_TBL (c cidr, i inet);
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1', '192.168.1.226/24');
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1.0/26', '192.168.1.226');
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1', '192.168.1.0/24');
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1', '192.168.1.0/25');
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1', '192.168.1.255/24');
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1', '192.168.1.255/25');
INSERT INTO INET_TBL (c, i) VALUES ('10', '10.1.2.3/8');
INSERT INTO INET_TBL (c, i) VALUES ('10.0.0.0', '10.1.2.3/8');
INSERT INTO INET_TBL (c, i) VALUES ('10.1.2.3', '10.1.2.3/32');
INSERT INTO INET_TBL (c, i) VALUES ('10.1.2', '10.1.2.3/24');
INSERT INTO INET_TBL (c, i) VALUES ('10.1', '10.1.2.3/16');
INSERT INTO INET_TBL (c, i) VALUES ('10', '10.1.2.3/8');
INSERT INTO INET_TBL (c, i) VALUES ('10', '11.1.2.3/8');
INSERT INTO INET_TBL (c, i) VALUES ('10', '9.1.2.3/8');
-- check that CIDR rejects invalid input:
INSERT INTO INET_TBL (c, i) VALUES ('192.168.1.2/30', '192.168.1.226');
-- check that CIDR rejects invalid input when converting from text:
INSERT INTO INET_TBL (c, i) VALUES (cidr('192.168.1.2/30'), '192.168.1.226');

SELECT '' AS ten, c AS cidr, i AS inet FROM INET_TBL ORDER BY c, i;

-- now test some support functions

SELECT '' AS ten, i AS inet, host(i), text(i), family(i) FROM INET_TBL ORDER BY c, i;
SELECT '' AS ten, c AS cidr, broadcast(c),
  i AS inet, broadcast(i) FROM INET_TBL ORDER BY c, i;
SELECT '' AS ten, c AS cidr, network(c) AS "network(cidr)",
  i AS inet, network(i) AS "network(inet)" FROM INET_TBL ORDER BY c, i;
SELECT '' AS ten, c AS cidr, masklen(c) AS "masklen(cidr)",
  i AS inet, masklen(i) AS "masklen(inet)" FROM INET_TBL ORDER BY c, i;

SELECT '' AS four, c AS cidr, masklen(c) AS "masklen(cidr)",
  i AS inet, masklen(i) AS "masklen(inet)" FROM INET_TBL
  WHERE masklen(c) <= 8 ORDER BY c, i;

SELECT '' AS six, c AS cidr, i AS inet FROM INET_TBL
  WHERE c = i ORDER BY c, i;

SELECT '' AS ten, i, c,
  i < c AS lt, i <= c AS le, i = c AS eq,
  i >= c AS ge, i > c AS gt, i <> c AS ne,
  i << c AS sb, i <<= c AS sbe,
  i >> c AS sup, i >>= c AS spe,
  i && c AS ovr
  FROM INET_TBL ORDER BY c, i;

SELECT max(i) AS max, min(i) AS min FROM INET_TBL;
SELECT max(c) AS max, min(c) AS min FROM INET_TBL;

-- check the conversion to/from text and set_netmask
SELECT '' AS ten, set_masklen(inet(text(i)), 24) FROM INET_TBL ORDER BY c, i;

SELECT * FROM inet_tbl WHERE i<<'192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i<<='192.168.1.0/24'::cidr ORDER BY c, i;

SELECT * FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <<= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i && '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >>= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >> '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i < '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i = '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i > '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <> '192.168.1.0/24'::cidr ORDER BY c, i;

EXPLAIN (COSTS OFF)
SELECT i FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT i FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;

SELECT * FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <<= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i && '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >>= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >> '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i < '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i = '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i >= '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i > '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT * FROM inet_tbl WHERE i <> '192.168.1.0/24'::cidr ORDER BY c, i;

EXPLAIN (COSTS OFF)
SELECT i FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;
SELECT i FROM inet_tbl WHERE i << '192.168.1.0/24'::cidr ORDER BY c, i;

-- simple tests of inet boolean and arithmetic operators
SELECT i, ~i AS "~i" FROM inet_tbl ORDER BY c, i;
SELECT i, c, i & c AS "and" FROM inet_tbl ORDER BY c, i;
SELECT i, c, i | c AS "or" FROM inet_tbl ORDER BY c, i;
SELECT i, i + 500 AS "i+500" FROM inet_tbl ORDER BY c, i;
SELECT i, i - 500 AS "i-500" FROM inet_tbl ORDER BY c, i;
SELECT i, c, i - c AS "minus" FROM inet_tbl ORDER BY c, i;
SELECT '127.0.0.1'::inet + 257;
SELECT ('127.0.0.1'::inet + 257) - 257;
SELECT '127.0.0.2'::inet  - ('127.0.0.2'::inet + 500);
SELECT '127.0.0.2'::inet  - ('127.0.0.2'::inet - 500);

-- these should give overflow errors:
SELECT '127.0.0.1'::inet + 10000000000;
SELECT '127.0.0.1'::inet - 10000000000;

DROP TABLE INET_TBL;