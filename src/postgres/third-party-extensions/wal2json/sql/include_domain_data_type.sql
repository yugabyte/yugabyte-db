\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;

CREATE DOMAIN "wal2json_DOMAIN_1" AS bigint;
CREATE DOMAIN wal2json_domain_2 AS numeric(5,3);
CREATE DOMAIN wal2json_domain_3 AS varchar(30);
CREATE DOMAIN "wal2json_DOMAIN_4" AS bit varying(20);

CREATE TYPE "wal2json_Type" AS ENUM('a', 'b', 'c');

CREATE TABLE test_wal2json_5 (
a	smallserial,
b	smallint,
c	int,
d	"wal2json_DOMAIN_1",
e	wal2json_domain_2,
f	real not null,
g	double precision,
h	char(10),
i	wal2json_domain_3,
j	text,
k	"wal2json_DOMAIN_4",
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector,
PRIMARY KEY(b, c, d)
);

CREATE TABLE test_wal2json_6 (
a	integer,
b	"wal2json_Type"[],
PRIMARY KEY(a)
);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

INSERT INTO test_wal2json_5 (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);

UPDATE test_wal2json_5 SET f = -f WHERE b = 1;

INSERT INTO test_wal2json_6 (a, b) VALUES(1, array['b', 'c']::"wal2json_Type"[]);

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-domain-data-type', '0', 'include-pk', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-domain-data-type', '1', 'include-pk', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-domain-data-type', '0', 'include-typmod', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'include-domain-data-type', '1', 'include-typmod', '0');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-domain-data-type', '0', 'include-pk', '1');
SELECT data FROM pg_logical_slot_get_changes('regression_slot', NULL, NULL, 'format-version', '2', 'include-domain-data-type', '1', 'include-pk', '1');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE test_wal2json_5;
DROP TABLE test_wal2json_6;
DROP DOMAIN "wal2json_DOMAIN_1";
DROP DOMAIN wal2json_domain_2;
DROP DOMAIN wal2json_domain_3;
DROP DOMAIN "wal2json_DOMAIN_4";
DROP TYPE "wal2json_Type";
