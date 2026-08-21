CREATE TABLE table_missing_attrs(id int);
INSERT INTO table_missing_attrs VALUES (1);

SELECT * FROM table_missing_attrs;
SELECT id from table_missing_attrs;

ALTER TABLE table_missing_attrs ADD COLUMN a int DEFAULT 10;
ALTER TABLE table_missing_attrs ADD COLUMN b int DEFAULT 20;
ALTER TABLE table_missing_attrs ADD COLUMN c int DEFAULT NULL;
ALTER TABLE table_missing_attrs ADD COLUMN d int DEFAULT 30;
ALTER TABLE table_missing_attrs ADD COLUMN e int DEFAULT 0;

SELECT * FROM table_missing_attrs;
SELECT a, c, d, e FROM table_missing_attrs;

INSERT INTO table_missing_attrs(id, a, b) VALUES (2, 100, 200);
ALTER TABLE table_missing_attrs ADD COLUMN f TEXT DEFAULT 'abcdefghijklmnopqrstuvwxyz';

SELECT * FROM table_missing_attrs;
SELECT a, c, d, f FROM table_missing_attrs;

DROP TABLE table_missing_attrs;
