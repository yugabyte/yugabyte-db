---
title: Indexes and constraints in YugabyteDB YCQL
headerTitle: Indexes and constraints
linkTitle: Indexes and constraints
description: Using indexes and constraints in YugabyteDB YCQL
headcontent: Explore indexes and constraints in YCQL
image: /images/section_icons/explore/monitoring.png
menu:
  preview:
    identifier: explore-indexes-constraints-ycql
    parent: explore-ycql-language
    weight: 245
type: indexpage
---

As with tables, indexes in YugabyteDB are stored in a distributed manner - that is, they are split into tablets and replicated. Updates to indexes are transactional, which means that row updates and the corresponding index updates occur as a single transaction. Similar to tables, they are stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format, as opposed to the [B-tree](https://www.postgresql.org/docs/current/btree-implementation.html#BTREE-STRUCTURE) structure used by indexes in PostgreSQL.

The [YCQL API](../../api/ycql/) supports most of the Cassandra index semantics while incorporating other improvements.

The following table lists different types of indexes and their support in YCQL.

| Type | YCQL | Description  |
| :--- | :--- | :--- |
| [Primary key](primary-key-ycql) | Yes | Unique key that identifies the row |
| Foreign key | No | Link to a column in another table |
| [Secondary index](secondary-indexes-ycql) | Yes | Index on columns other than the primary key |
| [Unique index](unique-index-ycql) | Yes | Set one or many columns to be unique |
| [Multi-column index](secondary-indexes-ycql/#multi-column-index) | Yes | Index on multiple columns for faster scan with lesser rows |
| [Partial index](partial-index-ycql) | Yes | Indexes that apply to only some rows of the table |
| [Covering index](covering-index-ycql) | Yes | Store other columns in the index for faster retrieval |
| Expression index | No | Index based on a functional operation on columns |
| GIN index | No | Generalized inverted index for fast text search |
| GIST Index | No | For spatial search. Tracked - {{<issue 1337>}} |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Primary keys"
    body="Explore the use of primary keys in YCQL."
    href="primary-key-ycql/"
    icon="fa-solid fa-bars">}}

  {{<index/item
    title="Secondary and multi-column indexes"
    body="Explore indexes to optimize your database performance with examples."
    href="secondary-indexes-ycql/"
    icon="fa-solid fa-list-ol">}}

  {{<index/item
    title="Unique indexes"
    body="Explore unique indexes in YCQL with examples."
    href="unique-index-ycql/"
    icon="fa-solid fa-bars-staggered">}}

  {{<index/item
    title="Partial indexes"
    body="Explore partial indexes in YCQL with examples."
    href="partial-index-ycql/"
    icon="fa-solid fa-list-check">}}

   {{<index/item
    title="Covering indexes"
    body="Explore Covering indexes in YCQL with examples."
    href="covering-index-ycql/"
    icon="fa-solid fa-table-list">}}

  {{<index/item
    title="Secondary indexes with JSONB"
    body="Create covering and partial indexes with JSONB columns."
    href="secondary-indexes-with-jsonb-ycql/"
    icon="fa-solid fa-list-ol">}}

{{</index/block>}}
