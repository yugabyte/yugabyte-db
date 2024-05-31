--
-- Table definition
--
CREATE TABLE airports(ident TEXT,
                      type TEXT,
                      name TEXT,
                      elevation_ft INT,
                      continent TEXT,
                      iso_country CHAR(2),
                      iso_region CHAR(7),
                      municipality TEXT,
                      gps_code TEXT,
                      iata_code TEXT,
                      local_code TEXT,
                      coordinates TEXT,
                      PRIMARY KEY (ident));

-- Index for SELECTing ybctid of the same airport country using HASH key.
CREATE INDEX airport_type_hash_idx ON airports(type HASH, iso_country ASC, ident ASC);

-- Index for SELECTing ybctid of the same airport type using RANGE key.
CREATE INDEX airport_type_range_idx ON airports(name ASC, type ASC, ident ASC);

-- Index for checking correctness when using secondary index.
CREATE INDEX airport_type_region_idx ON airports((type, iso_region) HASH, ident ASC);

--
-- SELECT using a hash index, "airport_type_hash_idx", to collect ybctids.
--

-- The following select use batches of ybctid that are selected from "airport_type_hash_idx"
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE type = 'large_airport' AND iso_country > '0';

-- The following select use one ybctid at a time to query data.
EXPLAIN (COSTS OFF) SELECT * FROM airports WHERE ident IN
  (SELECT ident FROM airports WHERE type = 'large_airport' AND iso_country > '0');

--
-- SELECT using a range index, "airport_type_range_idx", to collet ybctids.
--

-- The following select use batches of ybctid that are selected from "airport_type_range_idx"
EXPLAIN (COSTS OFF) SELECT elevation_ft FROM airports WHERE type = 'large_airport' AND name > '0';

-- The following select use one ybctid at a time to query data.
EXPLAIN (COSTS OFF) SELECT elevation_ft FROM airports WHERE ident IN
  (SELECT ident FROM airports WHERE type = 'large_airport' AND name > '0');
