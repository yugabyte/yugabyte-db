-- Drops basic tables from test/regress/sql/create_table.sql
q
DROP TABLE a_star;
DROP TABLE a_star CASCADE;

DROP TABLE aggtest;

DROP TABLE hash_i4_heap;

DROP TABLE hash_name_heap;

DROP TABLE hash_txt_heap;

DROP TABLE hash_f8_heap;

DROP TABLE bt_i4_heap;

DROP TABLE bt_name_heap;

DROP TABLE bt_txt_heap;

DROP TABLE bt_f8_heap;

DROP TABLE array_op_test;

DROP TABLE array_index_op_test;

DROP TABLE testjsonb;

DROP TABLE IF EXISTS test_tsvector;
DROP TABLE IF EXISTS test_tsvector;

DROP TABLE extra_wide_table;
