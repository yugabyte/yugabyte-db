<p align="center">
    <picture>
        <source media="(prefers-color-scheme: dark)" srcset="logo-dark.svg">
        <img width="800" src="logo-light.svg" alt="pg_duckdb logo" />
    </picture>
</p>

<p align="center">
    <strong>PostgreSQL extension for DuckDB</strong><br>
</p>

<p align="center">
    <a href="https://hub.docker.com/r/pgduckdb/pgduckdb"><img src="https://img.shields.io/docker/pulls/pgduckdb/pgduckdb?style=flat-square&logo=docker" alt="Docker Pulls"></a>
    <a href="https://github.com/duckdb/pg_duckdb/releases"><img src="https://img.shields.io/github/v/release/duckdb/pg_duckdb?style=flat-square&logo=github" alt="GitHub Release"></a>
    <a href="https://github.com/duckdb/pg_duckdb/blob/main/LICENSE"><img src="https://img.shields.io/github/license/duckdb/pg_duckdb?style=flat-square" alt="License"></a>
</p>

---

# pg_duckdb: Official PostgreSQL Extension for DuckDB

**pg_duckdb** integrates DuckDB's columnar-vectorized analytics engine into PostgreSQL, enabling high-performance analytics and data-intensive applications. Built in collaboration with [Hydra][hydra] and [MotherDuck][motherduck].

## Key Features

- **Execute analytics queries without changes**: run your existing SQL analytics queries as you normally would, and `pg_duckdb` will automatically use DuckDB's SQL engine to execute them when you set `duckdb.force_execution=true`.
- **Read/write data from data lakes**: Read/write* Parquet, CSV, JSON, Iceberg & Delta Lake from S3, GCS, Azure & R2.
- **Integration with cloud analytics**: Out-of-the-box support of [MotherDuck](https://motherduck.com/) as compute provider.

## How `pg_duckdb` works

`pg_duckdb` automatically accelerates your existing analytical queries.

- **No special syntax needed:** for queries on your regular PostgreSQL tables, you don't need to change your SQL. Just run your `SELECT` statements as you normally would, and `pg_duckdb` will use DuckDB's engine to execute them.
- **No data export required:** You do not need to export your data to Parquet or any other format. `pg_duckdb` works directly with your existing PostgreSQL tables.

## See it in action

### Querying your existing PostgreSQL data

This is the most common and straightforward use case. If you have a standard PostgreSQL table, you can query it using standard SQL.

**Example:**

Let's say you have a PostgreSQL table named `orders` (to create it, see [syntax guide](docs/gotchas_and_syntax.md#create-a-table)). To run an analytical query, you just write standard SQL, configure `duckdb.force_execution` and `pg_duckdb` will handle the rest.

```sql
SET duckdb.force_execution = true;
SELECT
    order_date,
    COUNT(*) AS number_of_orders,
    SUM(amount) AS total_revenue
FROM
    orders
GROUP BY
    order_date
ORDER BY
    order_date;
```

### Querying external data (your first data lake query)

`pg_duckdb` allows you to query external files (like Parquet or CSV) as if they were tables in your database. This is perfect for querying data lakes from `pg_duckdb`. To learn more on these functions, see [read functions documentation](docs/functions.md#read-functions).

```sql
-- Setup S3 access in seconds directly from SQL
SELECT duckdb.create_simple_secret(
    type := 'S3', key_id := 'your_key', secret := 'your_secret', region := 'us-east-1'
);

SELECT
    r['product_name'], -- 'r' is to iterate on the row object returned from read_parquet()
    AVG(r['rating']) AS average_rating
FROM
    read_parquet('s3://your-bucket/reviews.parquet') r
GROUP BY
    r['product_name']
ORDER BY
    average_rating DESC;
```

### Combining PostgreSQL and DuckDB data

You can easily join your PostgreSQL tables with external data from your data lake.

```sql
-- Join a PostgreSQL table with a remote Parquet file
SELECT
    o.product_name,
    o.total_revenue,
    r.average_rating
FROM
    (
        -- First, aggregate our local orders data
        SELECT
            product_name,
            SUM(amount) AS total_revenue
        FROM
            orders
        GROUP BY
            product_name
    ) o
JOIN
    (
        -- Then, aggregate our remote reviews data
        SELECT
            r['product_name'] AS product_name,
            AVG(r['rating']) AS average_rating
        FROM
            read_parquet('s3://your-bucket/reviews.parquet') r
        GROUP BY
            r['product_name']
    ) r ON o.product_name = r.product_name
ORDER BY
    o.total_revenue DESC;
```

### Modern DataLake Formats

Work with modern data formats like DuckLake, Iceberg and Delta Lake. To learn more, see [extensions documentation](docs/extensions.md).

```sql
-- Query Apache Iceberg tables with time travel
SELECT duckdb.install_extension('iceberg');
SELECT * FROM iceberg_scan('s3://warehouse/sales_iceberg', version := '2024-03-15-snapshot')

-- Process Delta Lake with schema evolution
SELECT duckdb.install_extension('delta');
SELECT * FROM delta_scan('s3://lakehouse/user_events')
```

### MotherDuck integration (optional)

`pg_duckdb` integrates with [MotherDuck](https://motherduck.com/), a cloud analytics platform. This allows you to run your queries on MotherDuck's powerful compute infrastructure, while still using your existing PostgreSQL tables.

To learn more, see [MotherDuck documentation](docs/motherduck.md).

```sql
-- Connect to MotherDuck
CALL duckdb.enable_motherduck('<your_motherduck_token>');
```

```sql
-- Your existing MotherDuck tables appear automatically
SELECT region, COUNT(*) FROM my_cloud_analytics_table;

-- Create cloud tables that sync across teams
CREATE TABLE real_time_kpis USING duckdb AS
SELECT
    date_trunc('day', created_at) as date,
    COUNT(*) as daily_signups,
    SUM(revenue) as daily_revenue
FROM user_events
GROUP BY date;
```

## Quick Start

### Docker

Run PostgreSQL with pg_duckdb pre-installed in a docker container:

```bash
docker run -d -e POSTGRES_PASSWORD=duckdb pgduckdb/pgduckdb:18-v1.1.0
```

With MotherDuck:
```bash
export MOTHERDUCK_TOKEN=<your_token>
docker run -d -e POSTGRES_PASSWORD=duckdb -e MOTHERDUCK_TOKEN pgduckdb/pgduckdb:18-v1.1.0
```

### Try with Hydra

You can also get started using [Hydra][hydra]:

```bash
pip install hydra-cli
hydra
```

### Package Managers

**pgxman (apt):**
```bash
pgxman install pg_duckdb
```

**Compile from source:**

```bash
git clone https://github.com/duckdb/pg_duckdb
cd pg_duckdb
make install
```

*See [compilation guide](docs/compilation.md) for detailed instructions.*

## Configuration

See [settings documentation](docs/settings.md) for complete configuration options.

## Documentation

| Topic | Description |
|-------|-------------|
| [Functions](docs/functions.md) | Complete function reference |
| [Syntax Guide & Gotchas](docs/gotchas_and_syntax.md) | Quick reference for common SQL patterns and things to know |
| [Types](docs/types.md) | Supported data types and advanced types usage |
| [MotherDuck](docs/motherduck.md) | Cloud integration guide |
| [Secrets](docs/secrets.md) | Credential management |
| [Extensions](docs/extensions.md) | DuckDB extension usage |
| [Transactions](docs/transactions.md) | Transaction behavior |
| [Compilation](docs/compilation.md) | Build from source |

**Note**: Advanced DuckDB types (STRUCT, MAP, UNION) require DuckDB execution context. Use `duckdb.query()` for complex type operations and `TEMP` tables for DuckDB table creation in most cases. See [Types documentation](docs/types.md) for details.

## Performance

pg_duckdb excels at:

- **Analytical Workloads**: Aggregations, window functions, complex JOINs
- **Data Lake Queries**: Scanning large Parquet/CSV files
- **Mixed Workloads**: Combining OLTP (PostgreSQL) with OLAP (DuckDB)
- **ETL Pipelines**: Transform and load data at scale

## Contributing

We welcome contributions! Please see:

- [Contributing Guidelines](CONTRIBUTING.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [Project Milestones][milestones] for upcoming features
- [Discussions][discussions] for feature requests
- [Issues][issues] for bug reports
- [Join the DuckDB Discord community](https://discord.duckdb.org/) then chat in [the #pg_duckdb channel](https://discord.com/channels/909674491309850675/1289177578237857802).

## Support

- **Documentation**: [Complete documentation][docs]
- **Community**: [DuckDB Discord #pg_duckdb channel](https://discord.com/channels/909674491309850675/1289177578237857802)
- **Issues**: [GitHub Issues][issues]
- **Commercial**: [Hydra][hydra] and [MotherDuck][motherduck] offer commercial support

## Requirements

- **PostgreSQL**: 14, 15, 16, 17, 18
- **Operating Systems**: Ubuntu 22.04-24.04, macOS

## License

Licensed under the [MIT License](LICENSE).

---

<p align="center">
    <strong>Built with ❤️</strong><br> in collaboration with <a href="https://hydra.so">Hydra</a> and <a href="https://motherduck.com">MotherDuck</a>
</p>

[milestones]: https://github.com/duckdb/pg_duckdb/milestones
[discussions]: https://github.com/duckdb/pg_duckdb/discussions
[issues]: https://github.com/duckdb/pg_duckdb/issues
[docs]: https://github.com/duckdb/pg_duckdb/tree/main/docs
[hydra]: https://hydra.so/
[motherduck]: https://motherduck.com/
