--
-- Table definition
-- Testing PrimaryKey and IndexKey whose columns are not in the same order with the table.
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
											PRIMARY KEY (iso_region, name, type, ident));

CREATE INDEX airports_scatter_idx ON airports((iso_region, type) HASH, coordinates, ident, name)
			 INCLUDE (gps_code);
