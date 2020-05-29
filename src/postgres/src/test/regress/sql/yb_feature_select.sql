--
-- YB_FEATURE Testsuite: SELECT
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
-- 
-- SELECT Statements
--
-- Select all.
SELECT * FROM feature_tab_dml_identifier ORDER BY col_id;
--
-- Select rows 1-5 and 11-15 from the table.
SELECT * FROM feature_tab_dml ORDER BY col_smallint LIMIT 5;
SELECT * FROM feature_tab_dml ORDER BY col_smallint OFFSET 10 FETCH FIRST 5 ROWS ONLY;
--
-- Select distinct
SELECT DISTINCT
				col_smallint,
				col_bigint,
				col_real,
				col_char,
				col_bytea,
				col_timestamp_tz,
				col_bool,
				col_array_int,
				col_array_text
		FROM feature_tab_dml
		WHERE col_smallint < 20
		ORDER BY col_bigint, col_smallint;
SELECT DISTINCT
				col_bigint,
				col_real,
				col_char,
				col_bytea,
				col_timestamp_tz,
				col_bool,
				col_array_int,
				col_array_text
		FROM feature_tab_dml
		WHERE col_smallint < 20
		ORDER BY col_bigint;
--
-- GROUP BY and HAVING clauses.
SELECT
				col_bigint,
				COUNT(col_smallint),
				AVG(col_real),
				SUM(col_array_int[1]),
				SUM(col_double)
		FROM feature_tab_dml
		GROUP BY col_bigint
		ORDER BY col_bigint;
--
SELECT
				col_bigint,
				COUNT(col_smallint),
				AVG(col_real),
				SUM(col_array_int[1]),
				SUM(col_double)
		FROM feature_tab_dml
		GROUP BY col_bigint HAVING SUM(col_double) < 18
		ORDER BY col_bigint;
--
-- WINDOW
SELECT DISTINCT
			 rank() OVER yuga_win yuga_rank,
			 count(col_integer) OVER yuga_win,
			 sum(col_bigint) OVER yuga_win,
			 avg(col_real) OVER yuga_win
	 FROM feature_tab_dml
	 WINDOW yuga_win AS (PARTITION BY col_integer ORDER BY col_smallint)
	 ORDER BY yuga_rank, count, sum, avg;
--
-- JOIN Test Cases
-- Table Join
SELECT
				t1.col_smallint,
				t1.col_array_text,
				t2.col_name
		FROM feature_tab_dml t1, feature_tab_dml_identifier t2
		WHERE t1.col_smallint = t2.col_id AND t2.col_name = t1.col_array_text[2]
        ORDER BY t1.col_smallint;
--
SELECT
				t1.col_smallint,
				t1.col_array_text,
				t2.col_name
		FROM feature_tab_dml t1, feature_tab_dml_identifier t2
		WHERE t1.col_smallint = t2.col_id AND
					(t2.col_name = 'nine' OR t2.col_name = 'seven')
        ORDER BY t1.col_smallint;
-- UNION
SELECT	col_smallint Employee_ID,
				col_text Employee_Name
		FROM feature_tab_dml
		WHERE col_smallint < 15
		UNION (SELECT col_id Employee_ID,
									col_name Employee_Name
							FROM feature_tab_dml_identifier
							WHERE col_id > 5)
		ORDER BY Employee_ID;
-- INTERSECT
SELECT	col_smallint Employee_ID,
				col_text Employee_Name
		FROM feature_tab_dml
		WHERE col_smallint < 15
		INTERSECT (SELECT col_id Employee_ID,
											col_name Employee_Name
									FROM feature_tab_dml_identifier
									WHERE col_id > 5)
		ORDER BY Employee_ID;
-- EXCEPT
SELECT	col_smallint Employee_ID,
				col_text Employee_Name
		FROM feature_tab_dml WHERE col_smallint < 15
		EXCEPT (SELECT col_id Employee_ID,
					 				 col_name Employee_Name
			 				 FROM feature_tab_dml_identifier
							 WHERE col_id > 5)
		ORDER BY Employee_ID;
