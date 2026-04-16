---
title: Packed rows in DocDB
headerTitle: Packed rows in DocDB
linkTitle: Packed rows
description: Packed rows implemention in DocDB
headcontent: Understand how packed rows are implemented in DocDB
menu:
  v2.25:
    identifier: docdb-packed-rows
    parent: docdb
    weight: 200
type: docs
---

Originally DocDB used to store individual column data of a row as multiple key-value pairs. Although this has advantages when single columns are looked up, it also meant multiple lookups for multiple columns and also led to increase in storage space. To overcome this, DocDB uses Packed rows, which means that an entire row is stored as a single key-value pair.

The packed row format for the YSQL API is {{<tags/feature/ga>}} as of v2.20.0, and for the YCQL API is {{<tags/feature/tp>}}.

## Overview

A row corresponding to the user table is stored as multiple key-value pairs in DocDB. For example, a row with one primary key `K` and `n` non-key columns, that is, `K (primary key)  |  C1 (column)  | C2  | ………  | Cn`, would be stored as `n` key-value pairs - `<K, C1> <K, C2> .... <K, Cn>`.

With packed row format, it would be stored as a single key-value pair: `<K, packed {C1, C2...Cn}>`.

While user-defined types (UDTs) can be used to achieve the packed row format at the application level, native support for packed row format has the following benefits:

* Lower storage footprint.
* Efficient INSERTs, especially when a table has large number of columns.
* Faster multi-column reads, as the reads need to fetch fewer key value pairs.
* UDTs require application rewrite, and therefore not necessarily an option for all use cases, like latency sensitive update workloads.

## Enable packed row

Packed row for YSQL is enabled by default for new universes in v2.20.0 and later.

If you upgrade a universe that does not have packed row enabled to a version earlier than v2025.1, packed row for YSQL _is not_ automatically enabled.

If you upgrade a universe that does not have packed row enabled to v2025.1 or later, packed row for YSQL is automatically enabled.

You can configure packed row format using the [Packed row flags](../../../reference/configuration/yb-tserver/#packed-row-flags).

## Design

Following are the design aspects of packed row format:

* **Inserts**: Entire row is stored as a single key-value pair.

* **Updates**:  If some column(s) are updated, then each such column update is stored as a key-value pair in DocDB (same as without packed rows). However, if all non-key columns are updated, then the row is stored in the packed format as one single key-value pair. This scheme adopts both efficient updates and efficient storage.

* **Select**: Scans need to construct the row from packed inserts as well as non-packed update(s), if any.

* **Point lookups**: Point lookups will be just as fast as without packed row as fundamentally, we will still be seeking a single key-value pair from DocDB.

* **Compactions**: Compactions produce a compact version of the row, if the row has unpacked fragments due to updates.

* **Backward compatibility**: Read code can interpret non-packed format as well. Writes/updates can produce non-packed format as well. Once a row is packed, it cannot be unpacked.

## Performance

Testing the packed row feature with different configurations showed significant performance gains:

* Sequential scans for table with 1 million rows was 2x faster with packed rows.
* Bulk ingestion of 1 million rows was 2x faster with packed rows.

## Limitations

Packed row for the YSQL API works across all features, including backup and restore, schema changes, and so on, subject to the following limitation:

* If you upgrade a universe that does not have packed row enabled to a version earlier than v2025.1, packed row for YSQL is not automatically enabled. This is due to a known limitation with xCluster universes, where the target universe might not be able to interpret the packed row unless it is upgraded first. {{<issue 21131>}}

Packed row for the YCQL API is {{<tags/feature/tp>}}. There are no known limitations.
