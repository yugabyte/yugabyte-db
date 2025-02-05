--
-- Partitions
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Drops tables created from create_table_partitions.sql.

DROP TABLE list_parted;

DROP TABLE bools;

DROP TABLE moneyp;

DROP TABLE bigintp;

DROP TABLE range_parted, hash_parted;

DROP TABLE no_oids_parted;

-- DROP TABLE oids_parted, part_forced_oids;

DROP TABLE list_parted2;

DROP TABLE range_parted2;

DROP TABLE range_parted3;

DROP TABLE hash_parted2;
