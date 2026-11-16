--
-- Operator classes whose input type differs from the column type.
--
-- An lsm index key is stored in the encoding of the column type, so DocDB
-- compares it as the column type does, not as the operator class does.
--

-- Rejected: bpchar_ops ignores trailing spaces, but a text key keeps them.
CREATE TABLE opc_t (k int PRIMARY KEY, s text, i int, b bit(3));
CREATE UNIQUE INDEX ON opc_t (s bpchar_ops);
-- Rejected: oid_ops sorts as unsigned, but an int4 key sorts as signed.
CREATE INDEX ON opc_t (i oid_ops ASC);
-- A type that cannot be an lsm key keeps its existing error.
CREATE INDEX ON opc_t (b varbit_ops);
-- Rejected: the table's primary key, and an index that ALTER COLUMN TYPE
-- rebuilds for the new column type.
CREATE TABLE opc_pk (s text, PRIMARY KEY (s bpchar_ops));
CREATE TABLE opc_alter (k int PRIMARY KEY, c char(5));
CREATE INDEX ON opc_alter (c bpchar_pattern_ops ASC);
ALTER TABLE opc_alter ALTER COLUMN c TYPE text;
-- Accepted in a restore, which recreates such an index from a backup.
SET yb_binary_restore = true;
CREATE INDEX opc_t_restored ON opc_t (s bpchar_ops);
RESET yb_binary_restore;

-- Accepted: the operator class compares the same way as the column type.
CREATE TYPE opc_enum AS ENUM ('b', 'a');
CREATE TABLE opc_ok (k int PRIMARY KEY, c char(5), v varchar(10), r regclass,
					 e opc_enum);
CREATE INDEX ON opc_ok (c bpchar_ops);
CREATE INDEX ON opc_ok (v ASC);
CREATE INDEX ON opc_ok (r ASC);
CREATE INDEX ON opc_ok (e ASC);
-- Accepted: a temporary table gets a PostgreSQL btree index, which compares
-- with the operator class, so 'a ' is a duplicate of 'a'.
CREATE TEMP TABLE opc_temp (k int, s text);
CREATE UNIQUE INDEX ON opc_temp (s bpchar_ops);
INSERT INTO opc_temp VALUES (1, 'a'), (2, 'a ');

DROP TABLE opc_t, opc_alter, opc_ok, opc_temp;
DROP TYPE opc_enum;
