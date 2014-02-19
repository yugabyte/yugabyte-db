-- this is the first test (CREATE EXTENSION, no DROP TABLE)
CREATE EXTENSION test_decoding;
-- predictability
SET synchronous_commit = on;

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

CREATE TABLE table_without_pk (
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
p	tsvector
);

CREATE TABLE table_with_unique (
a	smallserial,
b	smallint,
c	int,
d	bigint,
e	numeric(5,3) not null,
f	real not null,
g	double precision not null,
h	char(10),
i	varchar(30),
j	text,
k	bit varying(20),
l	timestamp,
m	date,
n	boolean not null,
o	json,
p	tsvector,
UNIQUE(g, n)
);

SELECT 'init' FROM pg_create_decoding_replication_slot('regression_slot', 'wal2json');

-- INSERT
BEGIN;
INSERT INTO table_with_pk (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
INSERT INTO table_without_pk (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
INSERT INTO table_with_unique (b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) VALUES(1, 2, 3, 3.54, 876.563452345, 1.23, 'teste', 'testando', 'um texto longo', B'001110010101010', '2013-11-02 17:30:52', '2013-02-04', true, '{ "a": 123 }', 'Old Old Parr'::tsvector);
COMMIT;

SELECT data FROM pg_decoding_slot_get_changes('regression_slot', 'now', 'include-xids', '0');
SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');
