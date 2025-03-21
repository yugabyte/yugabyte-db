--
-- Table definition
--
CREATE TABLE airports(ident TEXT,
											type TEXT,
											name TEXT,
											elevation_ft INT,
											continent TEXT,
											iso_country TEXT,
											iso_region TEXT,
											municipality TEXT,
											gps_code TEXT,
											iata_code TEXT,
											local_code TEXT,
											coordinates TEXT,
											PRIMARY KEY (iso_region HASH, ident ASC));

-- CREATE INDEX airports_idx ON airports(ident,
-- 			 			 							 						 type,
-- 																			 name,
-- 																			 elevation_ft,
-- 																			 continent,
-- 																			 iso_country,
-- 																			 iso_region,
-- 																			 municipality,
-- 																			 gps_code,
-- 																			 iata_code,
-- 																			 local_code,
-- 																			 coordinates);

CREATE INDEX airports_idx1 ON airports(iso_region hash, name DESC);
CREATE INDEX airports_idx2 ON airports(iso_region ASC, gps_code ASC);
CREATE INDEX airports_idx3 ON airports((iso_region, type) HASH, coordinates, ident, name)
			 INCLUDE (gps_code);
