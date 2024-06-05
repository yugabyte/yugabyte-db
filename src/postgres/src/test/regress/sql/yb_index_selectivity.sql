--
-- INDEX SELECTIVITY
--
--
-- The following queries can use any index depending on cost estimator.
--
EXPLAIN (COSTS OFF) SELECT count(*) FROM airports WHERE iso_region = 'US-CA';
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY ident;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY ident LIMIT 1;
--
-- The following query should use PRIMARY KEY index.
--
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY ident LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY ident ASC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports  WHERE iso_region = 'US-CA' ORDER BY ident DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' ORDER BY iso_region, ident LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND ident >= '4' LIMIT 2;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND ident < '4' LIMIT 2;
--
-- The following query should use "airports_idx1" index
--
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY name DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY name ASC LIMIT 1;
--
-- The following query should use "airports_idx2" index
--
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY gps_code ASC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY gps_code DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports ORDER BY iso_region, gps_code LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports ORDER BY iso_region ASC, gps_code ASC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports ORDER BY iso_region DESC, gps_code DESC LIMIT 1;
--
-- The following query should use "airports_idx3" index
--
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates ASC, ident, name LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident DESC, name DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name ASC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates LIMIT 1;
--
-- The following query also use "airports_idx3" index but not fully-covered.
--
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates ASC, ident, name LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident DESC, name DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name ASC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident LIMIT 1;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates LIMIT 1;
--
-- The following query should use "airports_idx3" index without LIMIT.
--
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates;
--
-- The following query should use "airports_idx3" index but not fully-covered and no LIMIT.
--
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident;
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates;
--
-- The following queries cannot be optimized. Either WHERE clause is missing or its given filter
-- cannot be optimized.
--
-- No where clause
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports;
-- Use '!=' on hash column.
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region != 'US-CA';
-- ORDER BY hash column.
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' ORDER BY type, coordinates;
-- ORDER BY in wrong direction.
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports ORDER BY iso_region ASC, gps_code DESC LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates DESC, ident, name LIMIT 1;
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA' AND type = 'small_airport'
          ORDER BY coordinates, ident, name DESC LIMIT 1;
-- HASH column is not completely specified while ordering by RANGE column
EXPLAIN (COSTS OFF) SELECT gps_code FROM airports WHERE iso_region = 'US-CA'
          ORDER BY type, coordinates LIMIT 1;
