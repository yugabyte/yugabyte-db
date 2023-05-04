-- ----------------------------------------------------------------
-- Regression tests for add aggregate hashval function.
-- ----------------------------------------------------------------
SELECT hll_set_output_version(1);

DROP TABLE IF EXISTS test_khvengxf;

CREATE TABLE test_khvengxf (
	val    integer
);

insert into test_khvengxf(val) values (1), (2), (3);

-- Check default and explicit signatures.

select hll_print(hll_add_agg(hll_hash_integer(val)))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, 512))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, -1))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, 512, 0))
       from test_khvengxf;

-- Check range checking.

select hll_print(hll_add_agg(hll_hash_integer(val), -1))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 32))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, -1))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 8))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, -2))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, 8589934592))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, 512, -1))
       from test_khvengxf;

select hll_print(hll_add_agg(hll_hash_integer(val), 10, 4, 512, 2))
       from test_khvengxf;

-- Check that we return hll_empty on null input.

select hll_print(hll_add_agg(NULL));

select hll_print(hll_add_agg(NULL, 10));

select hll_print(hll_add_agg(NULL, 10, 4));

select hll_print(hll_add_agg(NULL, 10, 4, 512));

select hll_print(hll_add_agg(NULL, 10, 4, -1));

select hll_print(hll_add_agg(NULL, 10, 4, 512, 0));

DROP TABLE test_khvengxf;
