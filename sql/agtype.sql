--
-- AGTYPE data type regression tests
--

--
-- Load extension and set path
--
LOAD 'agensgraph';
SET search_path TO ag_catalog;

--
-- Create a table using the AGTYPE type
--
CREATE TABLE agtype_table (type text, agtype agtype);

--
-- Insert values to exercise agtype_in/agtype_out
--
INSERT INTO agtype_table VALUES ('bool', 'true');
INSERT INTO agtype_table VALUES ('bool', 'false');

INSERT INTO agtype_table VALUES ('null', 'null');

INSERT INTO agtype_table VALUES ('string', '""');
INSERT INTO agtype_table VALUES ('string', '"This is a string"');

INSERT INTO agtype_table VALUES ('integer', '0');
INSERT INTO agtype_table VALUES ('integer', '9223372036854775807');
INSERT INTO agtype_table VALUES ('integer', '-9223372036854775808');

INSERT INTO agtype_table VALUES ('float', '0.0');
INSERT INTO agtype_table VALUES ('float', '1.0');
INSERT INTO agtype_table VALUES ('float', '-1.0');
INSERT INTO agtype_table VALUES ('float', '100000000.000001');
INSERT INTO agtype_table VALUES ('float', '-100000000.000001');
INSERT INTO agtype_table VALUES ('float', '0.00000000000000012345');
INSERT INTO agtype_table VALUES ('float', '-0.00000000000000012345');

INSERT INTO agtype_table VALUES ('integer array',
	'[-9223372036854775808, -1, 0, 1, 9223372036854775807]');
INSERT INTO agtype_table VALUES('float array',
	'[-0.00000000000000012345, -100000000.000001, -1.0, 0.0, 1.0, 100000000.000001, 0.00000000000000012345]');
INSERT INTO agtype_table VALUES('mixed array', '[true, false, null, "string", 1, 1.0, {"bool":true}]');

INSERT INTO agtype_table VALUES('object', '{"bool":true, "null":null, "string":"string", "integer":1, "float":1.2, "arrayi":[-1,0,1], "arrayf":[-1.0, 0.0, 1.0], "object":{"bool":true, "null":null, "string":"string", "int":1, "float":8.0}}');

--
-- Special float values: NaN, +/- Infinity
INSERT INTO agtype_table VALUES ('float  nan', 'nan');
INSERT INTO agtype_table VALUES ('float  Infinity', 'Infinity');
INSERT INTO agtype_table VALUES ('float -Infinity', '-Infinity');
INSERT INTO agtype_table VALUES ('float  inf', 'inf');
INSERT INTO agtype_table VALUES ('float -inf', '-inf');

SELECT * FROM agtype_table;

--
-- These should fail
--
INSERT INTO agtype_table VALUES ('bad integer', '9223372036854775808');
INSERT INTO agtype_table VALUES ('bad integer', '-9223372036854775809');
INSERT INTO agtype_table VALUES ('bad float', '-NaN');
INSERT INTO agtype_table VALUES ('bad float', 'Infi');
INSERT INTO agtype_table VALUES ('bad float', '-Infi');

--
-- Cleanup
--
DROP TABLE agtype_table;

--
-- End of AGTYPE data type regression tests
--
