--
-- INDEX SELECTIVITY
--
--
-- The following queries can use any index depending on cost estimator.
--
EXPLAIN SELECT count(*) FROM airports WHERE iso_region = 'US-CA';
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY ident;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY ident LIMIT 1;
--
-- The following query should use PRIMARY KEY index.
--
-- Currently, the first query chooses airports_idx2 incorrectly.  Cost-estimator needs to be fixed.
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY ident LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY ident ASC LIMIT 1;
EXPLAIN SELECT * FROM airports  WHERE iso_region = 'US-CA' ORDER BY ident DESC LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY iso_region, ident LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND ident >= '4' LIMIT 2;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND ident < '4' LIMIT 2;
--
-- The following query should use "airports_idx1" index
--
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY name DESC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY name ASC LIMIT 1;
--
-- The following query should use "airports_idx2" index
--
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY gps_code ASC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY gps_code DESC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports ORDER BY iso_region, gps_code LIMIT 1;
EXPLAIN SELECT gps_code FROM airports ORDER BY iso_region ASC, gps_code ASC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports ORDER BY iso_region DESC, gps_code DESC LIMIT 1;
--
-- The following query should use "airports_idx3" index
--
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates ASC, ident, name LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident DESC, name DESC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name ASC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates LIMIT 1;
--
-- The following query also use "airports_idx3" index but not fully-covered.
--
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates ASC, ident, name LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident DESC, name DESC LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name ASC LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident LIMIT 1;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates LIMIT 1;
--
-- The following query should use "airports_idx3" index without LIMIT.
--
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates;
--
-- The following query should use "airports_idx3" index but not fully-covered and no LIMIT.
--
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident;
EXPLAIN SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates;
--
-- The following queries cannot be optimized. Either WHERE clause is missing or its given filter
-- cannot be optimized.
--
-- No where clause
EXPLAIN SELECT gps_code FROM airports;
-- Use '!=' on hash column.
EXPLAIN SELECT gps_code FROM airports WHERE iso_region != 'US-CA';
-- ORDER BY hash column.
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY type, coordinates;
-- ORDER BY in wrong direction.
EXPLAIN SELECT gps_code FROM airports ORDER BY iso_region ASC, gps_code DESC LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident, name LIMIT 1;
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name DESC LIMIT 1;
-- HASH column is not completely specified while ordering by RANGE column
EXPLAIN SELECT gps_code FROM airports WHERE iso_region = 'US-CA'
          ORDER BY type, coordinates LIMIT 1;
