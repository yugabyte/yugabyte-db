\set VERBOSITY terse

-- predictability
SET synchronous_commit = on;
SET extra_float_digits = 0;



CREATE TABLE table_integer (a smallserial, b smallint, c int, d bigint);
CREATE TABLE table_decimal (a real, b double precision, c numeric);

SELECT 'init' FROM pg_create_logical_replication_slot('regression_slot', 'wal2json');

BEGIN;
INSERT INTO table_integer (b, c, d) VALUES(32767, 2147483647, 9223372036854775807);
INSERT INTO table_integer (b, c, d) VALUES(-32768, -2147483648, -9223372036854775808);

INSERT INTO table_decimal (a, b) VALUES('Infinity', 'Infinity');
INSERT INTO table_decimal (a, b) VALUES('-Infinity', '-Infinity');
INSERT INTO table_decimal (a, b, c) VALUES('NaN', 'NaN', 'NaN');
INSERT INTO table_decimal (a, b, c) VALUES(123.456, 123456789.012345, 1234567890987654321.1234567890987654321);
INSERT INTO table_decimal (a, b, c) VALUES(-123.456, -123456789.012345, -1234567890987654321.1234567890987654321);
COMMIT;

SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1', 'numeric-data-types-as-string', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '1', 'pretty-print', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2', 'numeric-data-types-as-string', '1');
SELECT data FROM pg_logical_slot_peek_changes('regression_slot', NULL, NULL, 'format-version', '2');

SELECT 'stop' FROM pg_drop_replication_slot('regression_slot');

DROP TABLE table_integer;
DROP TABLE table_decimal;
