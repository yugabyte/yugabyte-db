\x on
SET bytea_output = 'escape';
SELECT * FROM duckdb.query($$
FROM test_all_types()
SELECT * exclude(
    small_enum,
    medium_enum,
    large_enum,
    struct,
    struct_of_arrays,
    array_of_structs,
    fixed_nested_int_array,
    fixed_nested_varchar_array,
    fixed_struct_array,
    struct_of_fixed_array,
    fixed_array_of_int_list,
    list_of_fixed_int_array,
    nested_int_array, -- The nested array has different lengths, which is not possible in PG
    date, -- the min/max values of dates overflow in Postgres so selecting these would throw an error
    timestamp, -- the min/max values of timestamps overflow in Postgres so selecting these would throw an error
    timestamp_s, -- the min/max values of timestamps overflow in Postgres so selecting these would throw an error
    timestamp_ms, -- the min/max values of timestamps overflow in Postgres so selecting these would throw an error
    timestamp_ns, -- the min/max values of timestamps overflow in Postgres so selecting these would throw an error
    timestamp_tz -- the min/max values of timestamps overflow in Postgres so selecting these would throw an error
)
$$)
