--
-- CREATE_TABLE
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
											PRIMARY KEY (ident));
