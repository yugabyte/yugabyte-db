--
-- Partitions
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Drops tables created from create_table_partitions2.sql.

drop table parted_notnull_inh_test;

drop table parted_collate_must_match;

DROP TABLE unbounded_range_part;

DROP TABLE range_parted4;

DROP TABLE parted;
-- DROP TABLE partkey_t;
-- DROP OPERATOR CLASS test_int4_ops USING btree;
-- DROP FUNCTION my_int4_sort(int4,int4);

drop table perm_parted cascade;
