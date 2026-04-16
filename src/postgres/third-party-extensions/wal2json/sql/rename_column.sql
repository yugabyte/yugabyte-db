\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

CREATE TABLE w2j_rename_pk (
a	int,
b	timestamp,
c	text,
d	boolean,
e	numeric(5,3),
PRIMARY KEY(a, d)
);

CREATE TABLE w2j_rename_ri (
a	int NOT NULL,
b	timestamp,
c	text,
d	boolean NOT NULL,
e	numeric(5,3)
);
CREATE UNIQUE INDEX w2j_rename_ri_idx ON w2j_rename_ri (a, d);
ALTER TABLE w2j_rename_ri REPLICA IDENTITY USING INDEX w2j_rename_ri_idx;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO w2j_rename_pk (a, b, c, d, e) VALUES(123, '2020-04-26 16:23:59', 'Melanosuchus Niger', true, 4.56);
UPDATE w2j_rename_pk SET e = 8.76 WHERE a = 123;

ALTER TABLE w2j_rename_pk RENAME COLUMN d TO f;

INSERT INTO w2j_rename_pk (a, b, c, f, e) VALUES(456, '2020-12-07 15:56:59', 'Panthera Onca', false, 4.44);
UPDATE w2j_rename_pk SET e = 2.718 WHERE a = 456;

BEGIN;
INSERT INTO w2j_rename_pk (a, b, c, f, e) VALUES(789, '2021-04-04 10:33:04', 'Chrysocyon brachyurus', true, 20.30);
ALTER TABLE w2j_rename_pk RENAME COLUMN a TO g;
INSERT INTO w2j_rename_pk (g, b, c, f, e) VALUES(790, '2020-04-04 10:34:55', 'Myrmecophaga tridactyla', false, 1.8);
UPDATE w2j_rename_pk SET e = 3.1415 WHERE g = 456;
COMMIT;

INSERT INTO w2j_rename_ri (a, b, c, d, e) VALUES(123, '2020-04-26 16:23:59', 'Melanosuchus Niger', true, 4.56);
UPDATE w2j_rename_ri SET e = 8.76 WHERE a = 123;

ALTER TABLE w2j_rename_ri RENAME COLUMN d TO f;

INSERT INTO w2j_rename_ri (a, b, c, f, e) VALUES(456, '2020-12-07 15:56:59', 'Panthera Onca', false, 4.44);
UPDATE w2j_rename_ri SET e = 2.718 WHERE a = 456;

BEGIN;
INSERT INTO w2j_rename_ri (a, b, c, f, e) VALUES(789, '2021-04-04 10:33:04', 'Chrysocyon brachyurus', true, 20.30);
ALTER TABLE w2j_rename_ri RENAME COLUMN a TO g;
INSERT INTO w2j_rename_ri (g, b, c, f, e) VALUES(790, '2020-04-04 10:34:55', 'Myrmecophaga tridactyla', false, 1.8);
UPDATE w2j_rename_ri SET e = 3.1415 WHERE g = 456;
COMMIT;

ALTER TABLE w2j_rename_pk REPLICA IDENTITY FULL;
INSERT INTO w2j_rename_pk (g, b, c, f, e) VALUES(890, '2023-10-31 03:06:00', 'Crypturellus parvirostris', true, 8.90);
UPDATE w2j_rename_pk SET e = 8.91 WHERE g = 890;
DELETE FROM w2j_rename_pk WHERE g = 890;


SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0', 'include-pk', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-pk', '1');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE w2j_rename_pk;
DROP TABLE w2j_rename_ri;
