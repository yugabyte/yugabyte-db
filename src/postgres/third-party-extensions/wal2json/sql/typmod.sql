\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

DROP TABLE IF EXISTS table_with_pk;

CREATE TABLE table_with_pk (
a	smallserial,
b	smallint,
c	int,
d	bigint,
e	numeric(5,3),
f	real not null,
g	double precision,
h	char(10),
i	varchar(30),
j	text,
k	bit varying(20),
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector,
PRIMARY KEY(b, c, d)
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO table_with_pk (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);

UPDATE table_with_pk SET f = -f WHERE b = 1;

-- UPDATE: pk change
DELETE FROM table_with_pk WHERE b = 1;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-typmod', '0');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
