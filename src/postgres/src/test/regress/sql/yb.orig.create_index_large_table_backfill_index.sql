--
-- CREATE_INDEX
--
-- Index on airport names.
CREATE INDEX airports_idx_name ON airports(name ASC);

-- Index for SELECTing ybctid of the same airport country using HASH key.
CREATE INDEX airport_type_hash_idx ON airports(type HASH, iso_country ASC, ident ASC);

-- Index for SELECTing ybctid of the same airport type using RANGE key.
CREATE INDEX airport_name_type_ident_range_idx ON airports(name ASC, type ASC, ident ASC);

-- Index for checking correctness when using secondary index.
CREATE INDEX airport_type_region_idx ON airports((type, iso_region) HASH, ident ASC);
