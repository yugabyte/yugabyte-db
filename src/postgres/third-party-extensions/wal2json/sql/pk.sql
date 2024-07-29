\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

CREATE TABLE w2j_pk_with_pk (
a	int,
b	timestamp,
c	text,
d	boolean,
e	numeric(5,3),
PRIMARY KEY(b, d, e)
);

CREATE TABLE w2j_pk_without_pk (
a	int,
b	timestamp,
c	text,
d	boolean,
e	numeric(5,3)
);

CREATE TABLE w2j_pk_with_ri (
a	int NOT NULL,
b	timestamp,
c	text,
d	boolean,
e	numeric(5,3),
UNIQUE(a)
);
ALTER TABLE w2j_pk_with_ri REPLICA IDENTITY USING INDEX w2j_pk_with_ri_a_key;

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO w2j_pk_with_pk (a, b, c, d, e) VALUES(123, '2020-04-26 16:23:59', 'Melanosuchus Niger', true, 4.56);
UPDATE w2j_pk_with_pk SET a = 456, c = 'Panthera Onca', d = false;
DELETE FROM w2j_pk_with_pk;

INSERT INTO w2j_pk_without_pk (a, b, c, d, e) VALUES(123, '2020-04-26 16:23:59', 'Melanosuchus Niger', true, 4.56);
UPDATE w2j_pk_without_pk SET a = 456, c = 'Panthera Onca', d = false;
DELETE FROM w2j_pk_without_pk;

INSERT INTO w2j_pk_with_ri (a, b, c, d, e) VALUES(123, '2020-04-26 16:23:59', 'Inia Araguaiaensis', true, 4.56);
UPDATE w2j_pk_with_ri SET a = 456, c = 'Panthera Onca', d = false;
DELETE FROM w2j_pk_with_ri;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0', 'include-pk', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-pk', '1');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE w2j_pk_with_pk;
DROP TABLE w2j_pk_without_pk;
DROP TABLE w2j_pk_with_ri;
