CREATE EXTENSION postgres_fdw;

--
-- Test to validate behavior of 'server_type' option in foreign server.
--
CREATE SERVER s_yugabytedb FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'yugabytedb');
CREATE SERVER s_postgres FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'postgresql');
CREATE SERVER s_yugabytedb2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (dbname 'yugabyte', server_type 'yugabytedb');
CREATE SERVER s_nosrvtype FOREIGN DATA WRAPPER postgres_fdw;
CREATE SERVER s_nosrvtype2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (dbname 'yugabyte', host '127.0.0.1');
CREATE SERVER s_invalidsrvtype FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'something_invalid');
CREATE SERVER s_invalidsrvtype2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type '');

SELECT srv.srvname, srv.srvoptions
FROM pg_foreign_server srv JOIN pg_foreign_data_wrapper fdw ON srv.srvfdw = fdw.oid
WHERE fdw.fdwname = 'postgres_fdw';

-- Adding a 'server_type' option where none exists.
ALTER SERVER s_nosrvtype2 OPTIONS (ADD server_type 'yugabytedb');
-- Adding a 'server_type' option where one already exists should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (ADD server_type 'postgresql');
-- Modifying 'server_type' option where one already exists should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (SET server_type 'postgresql');
-- Modifying 'server_type' option where one already exists but with an invalid value should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (SET server_type 'something_invalid');
-- Dropping 'server_type' option should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (DROP server_type);
-- Modifying or dropping 'server_type' option where one does not exist should be disallowed.
ALTER SERVER s_nosrvtype OPTIONS (SET server_type 'postgresql');
ALTER SERVER s_nosrvtype OPTIONS (DROP server_type);

SELECT srv.srvname, srv.srvoptions
FROM pg_foreign_server srv JOIN pg_foreign_data_wrapper fdw ON srv.srvfdw = fdw.oid
WHERE fdw.fdwname = 'postgres_fdw';

DROP SERVER s_yugabytedb2;
DROP SERVER s_nosrvtype2;

-- Test to validate that 'server_type' option is not applicable to foreign table.
CREATE FOREIGN TABLE ft_test (v INT) SERVER s_yugabytedb OPTIONS (table_name 't_test', server_type 'yugabytedb');

CREATE TABLE t_test (v INT);
INSERT INTO t_test (SELECT i FROM generate_series(1, 10) AS i);

-- Sanity test to assert that the server has indeed been configured.
DO $d$
    BEGIN
        EXECUTE $$ALTER SERVER s_yugabytedb
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
        EXECUTE $$ALTER SERVER s_postgres
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
        EXECUTE $$ALTER SERVER s_nosrvtype
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
    END;
$d$;

CREATE USER MAPPING FOR CURRENT_USER SERVER s_yugabytedb;
CREATE USER MAPPING FOR CURRENT_USER SERVER s_postgres;
CREATE USER MAPPING FOR CURRENT_USER SERVER s_nosrvtype;

CREATE FOREIGN TABLE ft_test1 (v INT) SERVER s_yugabytedb OPTIONS (table_name 't_test');
SELECT * FROM ft_test1 ORDER BY v;

CREATE FOREIGN TABLE ft_test2 (v INT) SERVER s_postgres OPTIONS (table_name 't_test');
SELECT * FROM ft_test2 ORDER BY v;

CREATE FOREIGN TABLE ft_test3 (v INT) SERVER s_nosrvtype OPTIONS (table_name 't_test');
SELECT * FROM ft_test3 ORDER BY v;

-- A join involving tables of different server types must not be pushed down.
EXPLAIN (VERBOSE, COSTS OFF) SELECT * FROM ft_test1, ft_test2, ft_test3 WHERE ft_test1.v = ft_test2.v AND ft_test1.v = ft_test3.v;
SELECT * FROM ft_test1, ft_test2, ft_test3 WHERE ft_test1.v = ft_test2.v AND ft_test1.v = ft_test3.v;

-- Modifying table level options should be allowed.
ALTER FOREIGN TABLE ft_test1 OPTIONS (SET table_name 't_othertest');
ALTER FOREIGN TABLE ft_test2 OPTIONS (DROP table_name, ADD batch_size '1000');
SELECT * FROM pg_foreign_table;
-- Setting an option should be disallowed if it doesn't already exist.
ALTER FOREIGN TABLE ft_test3 OPTIONS (SET batch_size '1000', SET table_name 't_othertest');
-- Dropping an option should be disallowed if it doesn't already exist.
ALTER FOREIGN TABLE ft_test2 OPTIONS (DROP table_name);
-- Adding an option should be disallowed if it already exists.
ALTER FOREIGN TABLE ft_test1 OPTIONS (ADD table_name 't_test');
SELECT * FROM pg_foreign_table;

-- Restore the original table name.
ALTER FOREIGN TABLE ft_test1 OPTIONS (SET table_name 't_test');
ALTER FOREIGN TABLE ft_test2 OPTIONS (ADD table_name 't_test', DROP batch_size);
SELECT * FROM pg_foreign_table;
