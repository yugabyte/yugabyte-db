--
-- Drops enums from yb/integration-tests/xcluster/sql/create_sequence.sql
--

DROP SEQUENCE sequence_test;
-- DROP SEQUENCE sequence_test2;
DROP SEQUENCE sequence_test5;
DROP SEQUENCE sequence_test6;
DROP SEQUENCE sequence_test7;
DROP SEQUENCE sequence_test8;
DROP SEQUENCE sequence_test9;
DROP SEQUENCE sequence_test10;
DROP SEQUENCE sequence_test11;
DROP SEQUENCE sequence_test12;
DROP SEQUENCE sequence_test13;
DROP SEQUENCE sequence_test14;

DROP SEQUENCE foo_seq_new;
DROP SEQUENCE foo_seq2_new;

DROP SEQUENCE seq;
DROP SEQUENCE seq2;

DROP SEQUENCE sequence_test_unlogged;

-- Sequences should get wiped out as well:
DROP TABLE serialTest1, serialTest2;

DROP SEQUENCE test_seq1;

DROP SEQUENCE schema1.my_sequence;
DROP SEQUENCE schema2.my_sequence;

DROP SEQUENCE owned_sequence;
DROP SEQUENCE owned_sequence2;
DROP TABLE owning_table;

DROP SEQUENCE schema2.sequence_changing_schemas;
DROP SEQUENCE schema2.sequence_changing_schemas2;

SET search_path TO my_schema;
DROP SEQUENCE my_sequence;
DROP TABLE my_table;
