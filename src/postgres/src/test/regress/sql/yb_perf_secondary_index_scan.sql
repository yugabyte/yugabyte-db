--
-- This file is to check correctness of data after applying secondary index scan optimization.
--
-- Table definition
--
-- CREATE TABLE airports(ident TEXT,
--                       type TEXT,
--                       name TEXT,
--                       elevation_ft INT,
--                       continent TEXT,
--                       iso_country CHAR(2),
--                       iso_region CHAR(7),
--                       municipality TEXT,
--                       gps_code TEXT,
--                       iata_code TEXT,
--                       local_code TEXT,
--                       coordinates TEXT,
--                       PRIMARY KEY (ident));
--
-- Index for SELECTing ybctid of the same airport country using HASH key.
-- CREATE INDEX airport_type_region_idx ON airports((type, iso_region) HASH, ident ASC);
--
--
-- The following queries are to ensure the data is in indexing order.
-- NOTE: In the above indexes, range column "ident" is in ASC.
--
-- Column 'ident' should be sorted in ASC for this SELECT
EXPLAIN SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
SELECT * FROM airports WHERE type = 'closed' AND iso_region = 'US-CA';
--
-- This query the first 10 rows.
EXPLAIN SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident LIMIT 10;
--
-- This query the last 10 rows.
EXPLAIN SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
SELECT * FROM airports WHERE type = 'medium_airport' AND iso_region = 'US-CA'
  ORDER BY ident DESC LIMIT 10;
