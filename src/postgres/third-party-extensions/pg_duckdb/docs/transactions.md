# Transactions in pg_duckdb

Multi-statement transactions are supported in pg_duckdb. There is one important restriction on this though, which is is currently necessary to ensure the expected ACID guarantees: You cannot write to both a Postgres table and a DuckDB table in the same transaction.

Similarly you are allowed to do DDL (like `CREATE`/`DROP TABLE`) on DuckDB tables inside a transaction, but it's not allowed to combine such statements with DDL involving Postgres objects.

Finally it's possible to disable this restriction completely, and allow writes to both DuckDB and Postgres in the same transaction by setting `duckdb.unsafe_allow_mixed_transactions` to `true`. **This is at your own risk** and can result in the transaction being committed only in DuckDB, but not in Postgres. This can lead to inconsistencies and even data loss. For example the following code might result in deleting the `duckdb_table`, while not copying its contents to `pg_table`:

```sql
BEGIN;
SET LOCAL duckdb.unsafe_allow_mixed_transactions TO true;
CREATE TABLE pg_table AS SELECT * FROM duckdb_table;
DROP TABLE duckdb_table;
COMMIT;
```
