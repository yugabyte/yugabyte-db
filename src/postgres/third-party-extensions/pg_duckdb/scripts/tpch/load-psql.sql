\copy region FROM 'region.csv' (FORMAT 'csv', force_not_null ('r_regionkey', 'r_name', 'r_comment'), quote '"', header 1, DELIMITER '|');
\copy nation FROM 'nation.csv' (FORMAT 'csv', force_not_null ('n_nationkey', 'n_name', 'n_regionkey', 'n_comment'), quote '"', header 1, DELIMITER '|');
\copy customer FROM 'customer.csv' (FORMAT 'csv', force_not_null ('c_custkey', 'c_name', 'c_address', 'c_nationkey', 'c_phone', 'c_acctbal', 'c_mktsegment', 'c_comment'), quote '"', header 1, DELIMITER '|');
\copy supplier FROM 'supplier.csv' (FORMAT 'csv', force_not_null ('s_suppkey', 's_name', 's_address', 's_nationkey', 's_phone', 's_acctbal', 's_comment'), quote '"', header 1, DELIMITER '|');
\copy part FROM 'part.csv' (FORMAT 'csv', force_not_null ('p_partkey', 'p_name', 'p_mfgr', 'p_brand', 'p_type', 'p_size', 'p_container', 'p_retailprice', 'p_comment'), quote '"', header 1, DELIMITER '|');
\copy partsupp FROM 'partsupp.csv' (FORMAT 'csv', force_not_null ('ps_partkey', 'ps_suppkey', 'ps_availqty', 'ps_supplycost', 'ps_comment'), quote '"', header 1, DELIMITER '|');
\copy orders FROM 'orders.csv' (FORMAT 'csv', force_not_null ('o_orderkey', 'o_custkey', 'o_orderstatus', 'o_totalprice', 'o_orderdate', 'o_orderpriority', 'o_clerk', 'o_shippriority', 'o_comment'), quote '"', header 1, DELIMITER '|');
\copy lineitem FROM 'lineitem.csv' (FORMAT 'csv', force_not_null ('l_orderkey', 'l_partkey', 'l_suppkey', 'l_linenumber', 'l_quantity', 'l_extendedprice', 'l_discount', 'l_tax', 'l_returnflag', 'l_linestatus', 'l_shipdate', 'l_commitdate', 'l_receiptdate', 'l_shipinstruct', 'l_shipmode', 'l_comment'), quote '"', header 1, DELIMITER '|');
ANALYZE region, nation, customer, supplier, part, partsupp, orders, lineitem;
