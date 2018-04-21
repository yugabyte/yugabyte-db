CREATE DATABASE mytest_on_insert;
\c mytest_on_insert;

CREATE TABLE tab_without_key(id INT, name VARCHAR);

INSERT INTO tab_without_key(id) VALUES(1);
INSERT INTO tab_without_key(name) VALUES('one');
INSERT INTO tab_without_key(id, name) VALUES(2, 'two');
INSERT INTO tab_without_key VALUES(1, 'one');

SELECT id FROM tab_without_key;
SELECT name FROM tab_without_key;
SELECT id, name FROM tab_without_key;
SELECT * FROM tab_without_key;

CREATE TABLE tab_with_one_col_key(id INT PRIMARY KEY, name VARCHAR);

INSERT INTO tab_with_one_col_key(id) VALUES(1);
INSERT INTO tab_with_one_col_key(name) VALUES('one');
INSERT INTO tab_with_one_col_key(id, name) VALUES(2, 'two');
INSERT INTO tab_with_one_col_key VALUES(1, 'one');

SELECT id FROM tab_with_one_col_key;
SELECT name FROM tab_with_one_col_key;
SELECT id, name FROM tab_with_one_col_key;
SELECT * FROM tab_with_one_col_key;

CREATE TABLE tab_with_multi_col_key(id INT,
                                    name VARCHAR,
                                    salary DOUBLE PRECISION, primary key(id, name));

INSERT INTO tab_with_multi_col_key(id) VALUES(1);
INSERT INTO tab_with_multi_col_key(name) VALUES('one');
INSERT INTO tab_with_multi_col_key(id, name) VALUES(2, 'two');
INSERT INTO tab_with_multi_col_key VALUES(3, 'three', 999.99);

SELECT id FROM tab_with_multi_col_key;
SELECT name FROM tab_with_multi_col_key;
SELECT id, name FROM tab_with_multi_col_key;
SELECT id, name, salary FROM tab_with_multi_col_key;
SELECT * FROM tab_with_multi_key;

DROP TABLE tab_without_key;
DROP TABLE tab_with_one_col_key;
DROP TABLE tab_with_multi_col_key;

\c pgsql_testdb;
DROP DATABASE mytest_on_insert;
