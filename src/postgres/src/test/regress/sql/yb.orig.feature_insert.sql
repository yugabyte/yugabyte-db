--
-- YB_FEATURE Testsuite: INSERT
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
-- 
-- Prepare two identical tables of all supported primitive types.
CREATE TABLE feature_tab_dml(
			 col_smallint			SMALLINT,
			 col_integer			INTEGER,
			 col_bigint				BIGINT,
			 col_real					REAL,
			 col_double				DOUBLE PRECISION,
			 col_char					CHARACTER(7),
			 col_varchar			VARCHAR(7),
			 col_text					TEXT,
			 col_bytea				BYTEA,
			 col_timestamp		TIMESTAMP(2),
			 col_timestamp_tz TIMESTAMP WITH TIME ZONE,
			 col_bool					BOOLEAN,
			 col_array_int		INTEGER[],
			 col_array_text		TEXT[],
			 PRIMARY KEY(col_smallint));
--
CREATE TABLE feature_tab_dml_identifier(
			 col_id			SMALLINT,
			 col_name		TEXT,
			 PRIMARY KEY(col_id));
--
CREATE TABLE feature_tab_dml_copy_data AS SELECT * FROM feature_tab_dml;
CREATE TABLE feature_tab_dml_copy_no_data AS SELECT * FROM feature_tab_dml WITH NO DATA;
--
-- INSERT VALUES
--
INSERT INTO feature_tab_dml VALUES(
			 1,
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
INSERT INTO feature_tab_dml_identifier VALUES(1, 'one');
INSERT INTO feature_tab_dml VALUES(
			 11,
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
INSERT INTO feature_tab_dml_identifier VALUES(11, 'eleven');
-- Duplicate INSERT
INSERT INTO feature_tab_dml VALUES(
			 1,
			 1,
			 1,
			 1.1,
			 1.1,
			 'one',
			 'one',
			 'one',
			 E'\\x11F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'January 1, 2019 01:01:01.1111',
			 'January 1, 2019 01:01:01.1111 PST AD',
			 TRUE,
			 '{ 1, 1, 1 }',
			 '{ "one", "one", "one" }');
--
INSERT INTO feature_tab_dml VALUES(
			 2,
			 2,
			 2,
			 2.2,
			 2.2,
			 'two',
			 'two',
			 'two',
			 E'\\x22F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'February 2, 2019 02:02:02.2222',
			 'February 2, 2019 02:02:02.2222 PST AD',
			 TRUE,
			 '{ 2, 2, 2 }',
			 '{ "two", "two", "two" }');
INSERT INTO feature_tab_dml_identifier VALUES(2, 'two');
INSERT INTO feature_tab_dml VALUES(
			 12,
			 2,
			 2,
			 2.2,
			 2.2,
			 'two',
			 'two',
			 'two',
			 E'\\x22F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'February 2, 2019 02:02:02.2222',
			 'February 2, 2019 02:02:02.2222 PST AD',
			 TRUE,
			 '{ 2, 2, 2 }',
			 '{ "two", "two", "two" }');
INSERT INTO feature_tab_dml_identifier VALUES(12, 'twelve');
--
INSERT INTO feature_tab_dml VALUES(
			 3,
			 3,
			 3,
			 3.3,
			 3.3,
			 'three',
			 'three',
			 'three',
			 E'\\x33F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'March 3, 2019 03:03:03.3333',
			 'March 3, 2019 03:03:03.3333 PST AD',
			 TRUE,
			 '{ 3, 3, 3 }',
			 '{ "three", "three", "three" }');
INSERT INTO feature_tab_dml_identifier VALUES(3, 'three');
INSERT INTO feature_tab_dml VALUES(
			 13,
			 3,
			 3,
			 3.3,
			 3.3,
			 'three',
			 'three',
			 'three',
			 E'\\x33F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'March 3, 2019 03:03:03.3333',
			 'March 3, 2019 03:03:03.3333 PST AD',
			 TRUE,
			 '{ 3, 3, 3 }',
			 '{ "three", "three", "three" }');
INSERT INTO feature_tab_dml_identifier VALUES(13, 'thirteen');
--
INSERT INTO feature_tab_dml VALUES(
			 4,
			 4,
			 4,
			 4.4,
			 4.4,
			 'four',
			 'four',
			 'four',
			 E'\\x44F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'April 4, 2019 04:04:04.4444',
			 'April 4, 2019 04:04:04.4444 PST AD',
			 TRUE,
			 '{ 4, 4, 4 }',
			 '{ "four", "four", "four" }');
INSERT INTO feature_tab_dml_identifier VALUES(4, 'four');
INSERT INTO feature_tab_dml VALUES(
			 14,
			 4,
			 4,
			 4.4,
			 4.4,
			 'four',
			 'four',
			 'four',
			 E'\\x44F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'April 4, 2019 04:04:04.4444',
			 'April 4, 2019 04:04:04.4444 PST AD',
			 TRUE,
			 '{ 4, 4, 4 }',
			 '{ "four", "four", "four" }');
INSERT INTO feature_tab_dml_identifier VALUES(14, 'fourteen');
--
INSERT INTO feature_tab_dml VALUES(
			 5,
			 5,
			 5,
			 5.5,
			 5.5,
			 'five',
			 'five',
			 'five',
			 E'\\x55F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'May 5, 2019 05:05:05.5555',
			 'May 5, 2019 05:05:05.5555 PST AD',
			 TRUE,
			 '{ 5, 5, 5 }',
			 '{ "five", "five", "five" }');
INSERT INTO feature_tab_dml_identifier VALUES(5, 'five');
INSERT INTO feature_tab_dml VALUES(
			 15,
			 5,
			 5,
			 5.5,
			 5.5,
			 'five',
			 'five',
			 'five',
			 E'\\x55F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'May 5, 2019 05:05:05.5555',
			 'May 5, 2019 05:05:05.5555 PST AD',
			 TRUE,
			 '{ 5, 5, 5 }',
			 '{ "five", "five", "five" }');
INSERT INTO feature_tab_dml_identifier VALUES(15, 'fifteen');
--
INSERT INTO feature_tab_dml VALUES(
			 6,
			 6,
			 6,
			 6.6,
			 6.6,
			 'six',
			 'six',
			 'six',
			 E'\\x66F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'June 6, 2019 06:06:06.6666',
			 'June 6, 2019 06:06:06.6666 PST AD',
			 TRUE,
			 '{ 6, 6, 6 }',
			 '{ "six", "six", "six" }');
INSERT INTO feature_tab_dml_identifier VALUES(6, 'six');
INSERT INTO feature_tab_dml VALUES(
			 16,
			 6,
			 6,
			 6.6,
			 6.6,
			 'six',
			 'six',
			 'six',
			 E'\\x66F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'June 6, 2019 06:06:06.6666',
			 'June 6, 2019 06:06:06.6666 PST AD',
			 TRUE,
			 '{ 6, 6, 6 }',
			 '{ "six", "six", "six" }');
INSERT INTO feature_tab_dml_identifier VALUES(16, 'sixteen');
--
INSERT INTO feature_tab_dml VALUES(
			 7,
			 7,
			 7,
			 7.7,
			 7.7,
			 'seven',
			 'seven',
			 'seven',
			 E'\\x77F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'July 7, 2019 07:07:07.7777',
			 'July 7, 2019 07:07:07.7777 PST AD',
			 TRUE,
			 '{ 7, 7, 7 }',
			 '{ "seven", "seven", "seven" }');
INSERT INTO feature_tab_dml_identifier VALUES(7, 'seven');
INSERT INTO feature_tab_dml VALUES(
			 17,
			 7,
			 7,
			 7.7,
			 7.7,
			 'seven',
			 'seven',
			 'seven',
			 E'\\x77F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'July 7, 2019 07:07:07.7777',
			 'July 7, 2019 07:07:07.7777 PST AD',
			 TRUE,
			 '{ 7, 7, 7 }',
			 '{ "seven", "seven", "seven" }');
INSERT INTO feature_tab_dml_identifier VALUES(17, 'seventeen');
--
INSERT INTO feature_tab_dml VALUES(
			 8,
			 8,
			 8,
			 8.8,
			 8.8,
			 'eight',
			 'eight',
			 'eight',
			 E'\\x88F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'August 8, 2019 08:08:08.8888',
			 'August 8, 2019 08:08:08.8888 PST AD',
			 TRUE,
			 '{ 8, 8, 8 }',
			 '{ "eight", "eight", "eight" }');
INSERT INTO feature_tab_dml_identifier VALUES(8, 'eight');
INSERT INTO feature_tab_dml VALUES(
			 18,
			 8,
			 8,
			 8.8,
			 8.8,
			 'eight',
			 'eight',
			 'eight',
			 E'\\x88F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'August 8, 2019 08:08:08.8888',
			 'August 8, 2019 08:08:08.8888 PST AD',
			 TRUE,
			 '{ 8, 8, 8 }',
			 '{ "eight", "eight", "eight" }');
INSERT INTO feature_tab_dml_identifier VALUES(18, 'eighteen');
--
INSERT INTO feature_tab_dml VALUES(
			 9,
			 9,
			 9,
			 9.9,
			 9.9,
			 'nine',
			 'nine',
			 'nine',
			 E'\\x99F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'September 9, 2019 09:09:09.9999',
			 'September 9, 2019 09:09:09.9999 PST AD',
			 TRUE,
			 '{ 9, 9, 9 }',
			 '{ "nine", "nine", "nine" }');
INSERT INTO feature_tab_dml_identifier VALUES(9, 'nine');
INSERT INTO feature_tab_dml VALUES(
			 19,
			 9,
			 9,
			 9.9,
			 9.9,
			 'nine',
			 'nine',
			 'nine',
			 E'\\x99F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'September 9, 2019 09:09:09.9999',
			 'September 9, 2019 09:09:09.9999 PST AD',
			 TRUE,
			 '{ 9, 9, 9 }',
			 '{ "nine", "nine", "nine" }');
INSERT INTO feature_tab_dml_identifier VALUES(19, 'nineteen');
--
INSERT INTO feature_tab_dml(
			 			col_smallint,
			 			col_bigint,
			 			col_real,
			 			col_char,
			 			col_bytea,
			 			col_timestamp_tz,
			 			col_bool,
			 			col_array_int,
			 			col_array_text)
			 VALUES (
			 			10,
			 			0,
			 			0.0,
			 			'zero',
			 			E'\\x00F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 			'October 10, 2019 00:00:00.0000 PST AD',
			 			TRUE,
			 			'{ 0, 0, 0 }',
			 			'{ "zero", "zero", "zero" }')
			 RETURNING
						col_smallint,
						col_bigint,
						col_real,
						col_double,
						DATE_TRUNC('day', col_timestamp_tz) expr_date,
						col_array_text[1];
INSERT INTO feature_tab_dml_identifier VALUES(0, 'zero');
--
-- Use SELECT To Validate INSERT
--
SELECT * FROM feature_tab_dml ORDER BY col_smallint;
--
-- INSERT ON CONFLICT
--
INSERT INTO feature_tab_dml as t VALUES(
			 10,
			 0,
			 0,
			 0.0,
			 0.0,
			 'ten',
			 'ten',
			 'ten',
			 E'\\x00F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 'October 10, 2019 00:00:00.0000',
			 'October 10, 2019 00:00:00.0000 PST AD',
			 TRUE,
			 '{ 0, 0, 0 }',
			 '{ "ten", "ten", "ten" }')

 			 ON CONFLICT (col_smallint) DO UPDATE SET
			 		col_integer = t.col_integer + 10,
			 		col_bigint = t.col_bigint + 10,
			 		col_real = t.col_real + 10.10,
			 		col_double = t.col_double + 10.10,
			 		col_char = 'ten',
			 		col_varchar = 'ten',
			 		col_text = 'ten',
			 		col_bytea = E'\\x10F1E2D3C4B5A6079889706A5B4C3D2E1F',
			 		col_timestamp = 'October 10, 2019 10:10:10.0000',
			 		col_timestamp_tz = 'October 10, 2019 10:10:10.0000 PST AD',
			 		col_array_int = '{ 10, 10, 10 }',
			 		col_array_text = '{ "ten", "ten", "ten" }'
			 WHERE t.* != excluded.*;
INSERT INTO feature_tab_dml_identifier VALUES(10, 'ten');
--
-- Use SELECT To Validate INSERT ON CONFLICT
--
SELECT * FROM feature_tab_dml ORDER BY col_smallint;
--
-- INSERT WITH OVERRIDING
-- Need testing once supported.
--
