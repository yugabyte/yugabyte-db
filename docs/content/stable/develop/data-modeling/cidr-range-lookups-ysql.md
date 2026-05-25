---
title: Efficient CIDR range lookups in YugabyteDB YSQL
headerTitle: CIDR range lookups
linkTitle: CIDR range lookups
description: Use generated columns and secondary indexes for efficient CIDR range containment queries in YSQL
headContent: Use generated columns and secondary indexes to model efficient CIDR range lookups
menu:
  stable_develop:
    identifier: cidr-range-lookups-ysql
    parent: data-modeling
    weight: 320
tags:
  other: ysql
type: docs
---

YSQL supports PostgreSQL-compatible `inet` and `cidr` data types for storing IPv4 and IPv6 addresses. However, `inet` and `cidr` columns can't be used directly as index keys in YugabyteDB.

For efficient queries that find which CIDR range contains a given IP address, normalize addresses into a byte-comparable value, store the start and end of each CIDR range in generated columns, and create a secondary index on the range start.

This pattern works best when CIDR ranges don't overlap.

## Setup

Follow the [local cluster setup instructions](../../../quick-start/macos/) to start a local YugabyteDB cluster and connect to it using ysqlsh.

Create helper functions to normalize IPv4 and IPv6 addresses into a 16-byte binary representation. IPv4 addresses are stored as IPv6-mapped addresses so both address families can be compared consistently.

```sql
CREATE OR REPLACE FUNCTION inet_to_byte16(i inet) RETURNS bytea
 LANGUAGE sql IMMUTABLE RETURNS NULL ON NULL INPUT
AS $$
  SELECT substring(
    inet_send(
      CASE
        WHEN family(i) = 4 THEN ('::ffff:' || host(i))::inet
        ELSE i
      END
    )
    FROM 5
  );
$$;

CREATE OR REPLACE FUNCTION cidr_start_addr_16(c cidr) RETURNS bytea
 LANGUAGE sql IMMUTABLE RETURNS NULL ON NULL INPUT
AS $$
  SELECT inet_to_byte16(network(c)::inet);
$$;

CREATE OR REPLACE FUNCTION cidr_end_addr_16(c cidr) RETURNS bytea
 LANGUAGE sql IMMUTABLE RETURNS NULL ON NULL INPUT
AS $$
  SELECT inet_to_byte16(broadcast(c)::inet);
$$;
```

Create a table with generated columns for the start and end address of each CIDR range.

```sql
CREATE TABLE network_data (
  id int PRIMARY KEY,
  cidr_col cidr NOT NULL,
  cidr_start_addr bytea GENERATED ALWAYS AS (cidr_start_addr_16(cidr_col)) STORED,
  cidr_end_addr bytea GENERATED ALWAYS AS (cidr_end_addr_16(cidr_col)) STORED,
  payload text
);
```

Create an index on the start address and include the end address.

```sql
CREATE INDEX cidr_start_index
  ON network_data(cidr_start_addr ASC)
  INCLUDE (cidr_end_addr);
```

The indexed start address lets YugabyteDB seek into the range efficiently. Including the end address allows the storage layer to apply the range-end filter from the index before reading the base table.

## Add sample data

```sql
INSERT INTO network_data(id, cidr_col, payload)
VALUES
  (1, '10.0.0.0/8', 'row-1'),
  (2, '172.16.0.0/12', 'row-2'),
  (3, '192.168.1.0/24', 'row-3'),
  (4, '198.51.100.0/29', 'row-4'),
  (5, '127.0.0.0/8', 'row-5'),
  (6, '2001:db8::/32', 'row-6'),
  (7, 'fe80::/10', 'row-7'),
  (8, '2607:f8b0:4000::/48', 'row-8'),
  (9, '::1/128', 'row-9'),
  (10, '194.155.2.0/24', 'row-10');
```

## Query for a containing range

To find the CIDR range that contains an IP address, compare the normalized IP address with the generated start and end columns.

```sql
SELECT cidr_col, payload
FROM network_data
WHERE cidr_start_addr <= inet_to_byte16('194.155.2.7'::inet)
  AND cidr_end_addr >= inet_to_byte16('194.155.2.7'::inet)
ORDER BY cidr_start_addr DESC, cidr_col DESC
LIMIT 1;
```

```caddyfile{.nocopy}
    cidr_col     | payload
-----------------+---------
 194.155.2.0/24  | row-10
```

The descending order and `LIMIT 1` allow the database to seek backward from the target IP address and stop at the first matching range.

## Check the query plan

Use `EXPLAIN` to confirm that the query uses the index.

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF)
SELECT cidr_col, payload
FROM network_data
WHERE cidr_start_addr <= inet_to_byte16('194.155.2.7'::inet)
  AND cidr_end_addr >= inet_to_byte16('194.155.2.7'::inet)
ORDER BY cidr_start_addr DESC
LIMIT 1;
```

The plan should show a backward index scan using `cidr_start_index`. The `cidr_start_addr` predicate is used as the index condition, and the `cidr_end_addr` predicate can be applied from the included index column.

## Overlapping ranges

This example is optimized for non-overlapping CIDR ranges. If ranges can overlap, the query may need to scan more index entries before finding a match. You may also need additional ordering rules, such as preferring the most specific prefix.

## Learn more

- [Secondary indexes](../secondary-indexes-ysql/)
- [Expression indexes](../../../explore/ysql-language-features/indexes-constraints/expression-index-ysql/)
- [Covering indexes](../../../explore/ysql-language-features/indexes-constraints/covering-index-ysql/)
- [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index/)
