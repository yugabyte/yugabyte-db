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
