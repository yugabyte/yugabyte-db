--
-- JSONBX data type regression tests
--

--
-- Load extension and set path
--
LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- Create a table using the JSONBX type
--
CREATE TABLE jsonbx_table (type text, jsonbx jsonbx);

--
-- Insert values to exercise jsonbx_in/jsonbx_out
--
INSERT INTO jsonbx_table VALUES ('bool', 'true');
INSERT INTO jsonbx_table VALUES ('bool', 'false');

INSERT INTO jsonbx_table VALUES ('null', 'null');

INSERT INTO jsonbx_table VALUES ('string', '""');
INSERT INTO jsonbx_table VALUES ('string', '"This is a string"');

INSERT INTO jsonbx_table VALUES ('integer8', '0');
INSERT INTO jsonbx_table VALUES ('integer8', '9223372036854775807');
INSERT INTO jsonbx_table VALUES ('integer8', '-9223372036854775808');

INSERT INTO jsonbx_table VALUES ('float8', '0.0');
INSERT INTO jsonbx_table VALUES ('float8', '1.0');
INSERT INTO jsonbx_table VALUES ('float8', '-1.0');
INSERT INTO jsonbx_table VALUES ('float8', '100000000.000001');
INSERT INTO jsonbx_table VALUES ('float8', '-100000000.000001');
INSERT INTO jsonbx_table VALUES ('float8', '0.00000000000000012345');
INSERT INTO jsonbx_table VALUES ('float8', '-0.00000000000000012345');

INSERT INTO jsonbx_table VALUES ('integer8 array',
	'[-9223372036854775808, -1, 0, 1, 9223372036854775807]');
INSERT INTO jsonbx_table VALUES('float8 array',
	'[-0.00000000000000012345, -100000000.000001, -1.0, 0.0, 1.0, 100000000.000001, 0.00000000000000012345]');
INSERT INTO jsonbx_table VALUES('mixed array', '[true, false, null, "string", 1, 1.0, {"bool":true}]');

INSERT INTO jsonbx_table VALUES('object', '{"bool":true, "null":null, "string":"string", "integer8":1, "float8":1.2, "arrayi8":[-1,0,1], "arrayf8":[-1.0, 0.0, 1.0], "object":{"bool":true, "null":null, "string":"string", "int8":1, "float8":8.0}}');

SELECT * FROM jsonbx_table;

--
-- These should fail
--
INSERT INTO jsonbx_table VALUES ('bad integer8', '9223372036854775808');
INSERT INTO jsonbx_table VALUES ('bad integer8', '-9223372036854775809');
INSERT INTO jsonbx_table VALUES ('bad float8', 'NaN');
INSERT INTO jsonbx_table VALUES ('bad float8', 'Infinity');
INSERT INTO jsonbx_table VALUES ('bad float8', '-Infinity');

--
-- Cleanup
--
DROP TABLE jsonbx_table;

--
-- End of JSONBX data type regression tests
--
