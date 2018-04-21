CREATE DATABASE mytest_on_create_table;
\c mytest_on_create_table;

CREATE TABLE tab_without_key(id INT, name VARCHAR);

CREATE TABLE tab_with_one_col_key(id INT PRIMARY KEY, name VARCHAR);

CREATE TABLE tab_with_multi_col_key(id INT,
                                    name VARCHAR,
                                    salary DOUBLE PRECISION, primary key(id, name));

DROP TABLE tab_without_key;

DROP TABLE tab_with_one_col_key;

DROP TABLE tab_with_multi_col_key;

\c pgsql_testdb;
DROP DATABASE mytest_on_create_table;
