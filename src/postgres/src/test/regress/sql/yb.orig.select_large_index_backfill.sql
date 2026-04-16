EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
--
-- This queries the first 10 rows.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'small_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
SELECT * FROM airports WHERE type = 'small_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
--
-- This queries the last 10 rows.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'small_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
SELECT * FROM airports WHERE type = 'small_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
--
-- This queries the first 10 rows with names in a specified range.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE name > 'H' AND name < 'R' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
SELECT * FROM airports WHERE name > 'H' AND name < 'R' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
--
-- This queries the last 10 rows with names in a specified range.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE name > 'H' AND name < 'R' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
SELECT * FROM airports WHERE name > 'H' AND name < 'R' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
