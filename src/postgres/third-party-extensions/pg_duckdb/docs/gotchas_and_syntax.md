# pg_duckdb Syntax Guide & Gotchas

This guide provides a quick reference for the most common SQL patterns used with `pg_duckdb`, along with key behaviors and limitations to be aware of.

## Syntax Reference

### Create a table

```sql
-- This is a standard PostgreSQL table
CREATE TABLE orders (
    order_id SERIAL PRIMARY KEY,
    product_name TEXT,
    amount NUMERIC,
    order_date DATE
);

INSERT INTO orders (product_name, amount, order_date)
VALUES ('Laptop', 1200.00, '2024-07-01'),
       ('Keyboard', 75.50, '2024-07-01'),
       ('Mouse', 25.00, '2024-07-02');
```

### Querying standard PostgreSQL tables

For analytical queries on your existing PostgreSQL tables, use **standard SQL**. No special syntax is needed. `pg_duckdb` automatically accelerates these queries when you configure `duckdb.force_execution = true`.

```sql
SET duckdb.force_execution = true;
-- Standard SELECT on a PostgreSQL table
SELECT
    category,
    AVG(price) as avg_price,
    COUNT(*) as item_count
FROM
    products -- This is a regular PostgreSQL table
GROUP BY
    category
ORDER BY
    avg_price DESC;
```

### Querying external files (Parquet, CSV, etc.)

To query files from a data lake (e.g., S3, local storage), use the `read_*` functions. You must use the `r['column_name']` syntax to access columns.

```sql
-- Query a single Parquet file
SELECT
    r['product_id'],
    r['review_text']
FROM
    read_parquet('s3://my-bucket/reviews.parquet') r -- 'r' is a required alias
LIMIT 100;

-- Query multiple CSV files using a glob pattern
SELECT
    r['timestamp'],
    r['event_type'],
    COUNT(*) as event_count
FROM
    read_csv('s3://my-datalake/logs/2024-*.csv') r
GROUP BY
    r['timestamp'],
    r['event_type'];
```

### Hybrid queries (joining PostgreSQL and external data)

You can seamlessly join PostgreSQL tables with external data sources in a single query.

```sql
-- Join a local PostgreSQL 'customers' table with a remote Parquet file of 'orders'
SELECT
    c.customer_name,
    c.signup_date,
    SUM(r['order_total']) AS total_spent
FROM
    customers c -- This is a PostgreSQL table
JOIN
    read_parquet('s3://my-bucket/orders/*.parquet') r ON c.customer_id = r['customer_id']
WHERE
    c.status = 'active'
GROUP BY
    c.customer_name,
    c.signup_date
ORDER BY
    total_spent DESC;
```

### Creating MotherDuck-backed tables

To persist the results of an analytical query, you can create a new table that uses MotherDuck to store the data in columnar format. Use the `USING duckdb` clause.

```sql
-- Create a new table 'sales_summary' with DuckDB storage
CREATE TABLE sales_summary USING duckdb AS
SELECT
    r['region'],
    r['product_category'],
    SUM(r['sales_amount']) AS total_sales
FROM
    read_parquet('s3://my-datalake/sales_data/year=2024/**/*.parquet') r
GROUP BY
    r['region'],
    r['product_category'];

-- Query the newly created analytical table
SELECT * FROM sales_summary WHERE region = 'North America';
```

### When to use `duckdb.query()`

The `duckdb.query()` function is an **advanced feature** and is **not needed for most queries**. You should only use it when you need to run a query that uses DuckDB-specific syntax that is not valid in standard PostgreSQL.

**Example (Using DuckDB's `PIVOT` statement):**

```sql
-- This query uses DuckDB's PIVOT syntax, so it must be wrapped in duckdb.query()
SELECT * FROM duckdb.query($$
    PIVOT sales_summary
    ON product_category
    USING SUM(total_sales)
    GROUP BY region;
$$);
```

For all standard analytical queries, prefer the direct SQL syntax shown in the sections above.

---

## Things to Know

This section outlines behaviors and limitations in `pg_duckdb`, things to know to use the extension effectively.

### Querying

- **Standard SQL is the default**. For analytical queries on your PostgreSQL tables, use standard SQL. `pg_duckdb` accelerates them automatically as long as you configure `duckdb.force_execution=true`. No special functions are needed.

- **Use `r['column']` for external files**. When using `read_parquet()`, `read_csv()`, etc., you must access columns with the `r['column_name']` syntax, where `r` is the alias for the function call.

- **`duckdb.query()` is for special cases**. Only use `duckdb.query()` when you need DuckDB-specific syntax that PostgreSQL does not support, like `PIVOT`. It is not needed for regular queries.

- **CTE and subquery behavior**. When using `read_parquet()` inside a CTE or subquery, column references can behave differently. You may need to explicitly alias your columns (`SELECT r['col'] AS col`) or continue using the `r['col']` syntax outside the CTE.

- **Data type limitations**. [There are bunch of limitations in how data types behave because of differences between PostgreSQL and DuckDB](types.md#known-limitations)

### Transactions

- **Writes must be separate**. You cannot write to both a PostgreSQL table and a DuckDB table in the same transaction. This will cause an error. Perform these operations in separate transactions.

- **DDL must be separate**. You cannot mix `CREATE TABLE` statements for PostgreSQL and `CREATE TABLE ... USING duckdb` statements in the same transaction.

- **Avoid mixed transactions**. There is a setting `duckdb.unsafe_allow_mixed_transactions` to bypass the separate-write rule. This is not recommended as it can lead to data inconsistency if one part of the transaction fails.


### MotherDuck

- **Schema mapping is specific**. MotherDuck databases and schemas are mapped to PostgreSQL schemas with a special naming convention. Your default database's `main` schema maps to `public`. Other schemas are mapped to names like `ddb$<db_name>$<schema_name>`.

### Configuration

- **Forcing DuckDB execution**. For a query that only involves PostgreSQL tables, `pg_duckdb` will not run it by default. To force it, you can set `duckdb.force_execution = true`.

- **File system access is restricted**. By default, non-superusers cannot access the local file system. This is a security measure.
