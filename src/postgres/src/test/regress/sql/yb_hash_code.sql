SELECT yb_hash_code(1,2,3);
SELECT yb_hash_code(1,2,'abc'::text);
SELECT yb_hash_code('asdf');
SELECT yb_hash_code('{"a": {"b":{"c": "foo"}}}'::jsonb);

CREATE TABLE test_table_int (x INT PRIMARY KEY);
INSERT INTO test_table_int SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_int;
DROP TABLE test_table_int;

CREATE TABLE test_table_real (x REAL PRIMARY KEY);
INSERT INTO test_table_real SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_real;
DROP TABLE test_table_real;

CREATE TABLE test_table_double (x DOUBLE PRECISION PRIMARY KEY);
INSERT INTO test_table_double SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_double;
DROP TABLE test_table_double;

CREATE TABLE test_table_small (x SMALLINT PRIMARY KEY);
INSERT INTO test_table_small SELECT generate_series(1, 20);
SELECT yb_hash_code(x), x FROM test_table_small;
DROP TABLE test_table_small;

CREATE TABLE test_table_text (x TEXT PRIMARY KEY);
INSERT INTO test_table_text SELECT generate_series(800001, 800020);
SELECT yb_hash_code(x), x FROM test_table_text;
DROP TABLE test_table_text;

CREATE TYPE mood AS ENUM ('sad', 'ok', 'happy');
SELECT yb_hash_code('sad'::mood);
SELECT yb_hash_code('happy'::mood);
CREATE TABLE test_table_mood (x mood, y INT, PRIMARY KEY((x,y) HASH));
INSERT INTO test_table_mood VALUES ('sad'::mood, 1), ('happy'::mood, 4), 
('ok'::mood, 4), ('sad'::mood, 34), ('ok'::mood, 23);
SELECT yb_hash_code(x,y), * FROM test_table_mood;
DROP TABLE test_table_mood;
DROP TYPE mood;
