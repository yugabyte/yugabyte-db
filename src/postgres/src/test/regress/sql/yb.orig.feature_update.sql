--
-- YB_FEATURE Testsuite: UPDATE
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
-- 
-- Prepare two identical tables of all supported primitive types.
--
-- INSERT values to be updated
--
INSERT INTO feature_tab_dml VALUES(
			 77,
			 1,
			 1,
			 1.1,
			 1.1,
			 'one',
			 'one',
			 'one',
			 E'\\x11F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'January 1, 2019 01:11:11.1111',
			 'January 1, 2019 01:11:11.1111 PST AD',
			 TRUE,
			 '{ 1, 1, 1 }',
			 '{ "one", "one", "one" }');
INSERT INTO feature_tab_dml_identifier VALUES(77, 'seventy seven');
--
-- UPDATE Statement
--
UPDATE feature_tab_dml
			 SET
								col_integer = 77,
			 					col_bigint = 77,
			 					col_real = 77.77,
			 					col_double = 77.77,
			 					col_char = 'seven',
			 					col_varchar = 'seven',
			 					col_text = 'seven',
			 					col_bytea = E'\\x77F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 					col_timestamp = 'July 7, 2019 07:07:07.7777',
			 					col_timestamp_tz = 'July 7, 2019 07:07:07.7777 PST AD',
			 					col_bool = TRUE,
			 					col_array_int = '{ 77, 77, 77 }',
			 					col_array_text = '{ "seven", "seven", "seven" }'

			 WHERE
								col_smallint = 77
			 RETURNING
								col_smallint,
								col_bigint,
								col_real,
								col_double,
								DATE_TRUNC('day', col_timestamp_tz) expr_date,
								col_array_text[1];
--
-- Select updated rows.
--
SELECT * FROM feature_tab_dml WHERE col_smallint = 77;
--
-- INSERT empty row to check updating null values.
--
INSERT INTO feature_tab_dml VALUES(78);
UPDATE feature_tab_dml
			 SET
								col_integer = 78,
			 					col_bigint = 78,
			 					col_real = 78.78,
			 					col_double = 78.78,
			 					col_char = 'eight',
			 					col_varchar = 'eight',
			 					col_text = 'eight',
			 					col_bytea = E'\\x78F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 					col_timestamp = 'July 8, 2019 08:08:08.7878',
			 					col_timestamp_tz = 'July 8, 2019 08:08:08.7878 PST AD',
			 					col_bool = FALSE,
			 					col_array_int = '{ 78, 78, 78 }',
			 					col_array_text = '{ "eight", "eight", "eight" }'
			 WHERE
								col_smallint = 78;
--
--  Select updated rows.
--
SELECT * FROM feature_tab_dml WHERE col_smallint = 78;
--
-- UPDATE existing values to null.
--
UPDATE feature_tab_dml
			 SET
								col_integer = null,
			 					col_bigint = null,
			 					col_real = null,
			 					col_double = null,
			 					col_char = null,
			 					col_varchar = null,
			 					col_text = null,
			 					col_bytea = null,
			 					col_timestamp = null,
			 					col_timestamp_tz = null,
			 					col_bool = null,
			 					col_array_int = null,
			 					col_array_text = null
			 WHERE
								col_smallint = 78;
--
--  Select updated rows.
--
SELECT * FROM feature_tab_dml WHERE col_smallint = 78;

--
-- Test RETURNING clause with indexes and foreign keys.
--
CREATE TABLE feature_tab_dml_returning (
  id SERIAL PRIMARY KEY,
  a TEXT UNIQUE,
  b timestamp with time zone
);

INSERT INTO feature_tab_dml_returning (a) values ('foo');
SELECT * FROM feature_tab_dml_returning;

-- Using `NOW()` because it is not an immutable function, so it needs to be
-- evaluated during execution.
-- Do not select NOW() value to ensure expected output is stable, only
-- check query succeeds and b is not null (same for queries below).
UPDATE feature_tab_dml_returning SET b = NOW() where id = 1 RETURNING id, a, b IS NOT NULL;
SELECT id, a, b IS NOT NULL FROM feature_tab_dml_returning;

--
-- Add a table referencing column 'a'.
--
CREATE TABLE feature_tab_dml_returning_fk (
  id SERIAL NOT NULL PRIMARY KEY,
  x TEXT NOT NULL,
  y TEXT REFERENCES feature_tab_dml_returning(a) NOT NULL
);

-- Should succeed because no fkey references 'foo'
UPDATE feature_tab_dml_returning SET a = 'bar', b = NOW() where id = 1 RETURNING id, a, b IS NOT NULL;
SELECT id, a, b IS NOT NULL FROM feature_tab_dml_returning;

INSERT INTO feature_tab_dml_returning_fk (x, y) VALUES ('x', 'bar');
-- Should fail the foreign key check.
UPDATE feature_tab_dml_returning SET a = 'bar2', b = NOW() where id = 1 RETURNING id, a, b IS NOT NULL;
