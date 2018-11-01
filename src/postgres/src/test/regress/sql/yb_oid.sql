CREATE TABLE oid_table(
    id OID PRIMARY KEY,
    value OID,
    tag INT
);

INSERT INTO oid_table (id, value, tag) VALUES (0, 10, 0);
INSERT INTO oid_table (id, value, tag) VALUES (1, 21, 1);
INSERT INTO oid_table (id, value, tag) VALUES (1000, 1010, 2);
INSERT INTO oid_table (id, value, tag) VALUES (1000000, 1000010, 3);
INSERT INTO oid_table (id, value, tag) VALUES (1000000000, 1000000010, 4);
INSERT INTO oid_table (id, value, tag) VALUES (2000000000, 2000000020, 5);
INSERT INTO oid_table (id, value, tag) VALUES (2147483647, 2147483647, 6);
INSERT INTO oid_table (id, value, tag) VALUES (2147483648, 2147483648, 7);
INSERT INTO oid_table (id, value, tag) VALUES (2147483649, 2147483649, 8);
INSERT INTO oid_table (id, value, tag) VALUES (4294967295, 4294967295, 9);
INSERT INTO oid_table (id, value, tag) VALUES (4294967296, 4294967296, 10);
INSERT INTO oid_table (id, value, tag) VALUES (4294967297, 4294967297, 11);

INSERT INTO oid_table (id, value, tag) VALUES (-1, -10, 100);
INSERT INTO oid_table (id, value, tag) VALUES (-2, -21, 101);
INSERT INTO oid_table (id, value, tag) VALUES (-1000, -1010, 102);
INSERT INTO oid_table (id, value, tag) VALUES (-1000000, -1000010, 103);
INSERT INTO oid_table (id, value, tag) VALUES (-1000000000, -1000000010, 104);
INSERT INTO oid_table (id, value, tag) VALUES (-2000000000, -2000000020, 105);
INSERT INTO oid_table (id, value, tag) VALUES (-2147483647, -2147483647, 106);
INSERT INTO oid_table (id, value, tag) VALUES (-2147483648, -2147483648, 107);
INSERT INTO oid_table (id, value, tag) VALUES (-2147483649, -2147483649, 108);
INSERT INTO oid_table (id, value, tag) VALUES (-4294967295, -4294967295, 109);
INSERT INTO oid_table (id, value, tag) VALUES (-4294967296, -4294967296, 110);
INSERT INTO oid_table (id, value, tag) VALUES (-4294967297, -4294967297, 111);

SELECT * FROM oid_table ORDER BY id;
SELECT * FROM oid_table ORDER BY id DESC;
SELECT * FROM oid_table ORDER BY value;
SELECT * FROM oid_table ORDER BY value DESC;

DROP TABLE oid_table;
