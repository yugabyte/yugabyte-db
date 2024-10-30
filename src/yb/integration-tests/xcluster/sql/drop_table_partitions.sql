--
-- Partitions
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Drops tables created from create_table_partitions.sql.

DROP TABLE bools;

DROP TABLE moneyp;

DROP TABLE bigintp;

DROP TABLE unparted;

DROP TABLE no_oids_parted;

DROP TABLE oids_parted, part_forced_oids;
