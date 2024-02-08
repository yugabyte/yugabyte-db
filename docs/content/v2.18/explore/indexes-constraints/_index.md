---
title: Indexes and constraints in YugabyteDB
headerTitle: Indexes and constraints
linkTitle: Indexes and constraints
description: Using indexes and constraints in YugabyteDB
headcontent: Explore indexes and constraints in YSQL and YCQL
image: /images/section_icons/explore/monitoring.png
menu:
  v2.18:
    identifier: explore-indexes-constraints
    parent: explore
    weight: 245
type: indexpage
---

As with tables, indexes in YugabyteDB are stored in a distributed manner - that is, they are split into tablets and replicated. Updates to indexes are transactional, which means that row updates and the corresponding index updates occur as a single transaction. Similar to tables, they are stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format, as opposed to the [B-tree](https://www.postgresql.org/docs/current/btree-implementation.html#BTREE-STRUCTURE) structure used by indexes in PostgreSQL.

YugabyteDB supports most of the PostgreSQL index semantics in the [YSQL API](../../api/ysql/), and the [YCQL API](../../api/ycql/) supports most of the Cassandra index semantics while incorporating other improvements.

The following table lists different types of indexes and their support across the two APIs.

| Type | YSQL | YCQL | Description  |
| :--- | :--- | :--- | :--- |
| [Primary key](primary-key-ysql) | Yes | Yes | Unique key that identifies the row |
| [Foreign key](foreign-key-ysql) | Yes | No | Link to a column in another table |
| [Secondary index](secondary-indexes) | Yes | Yes | Index on columns other than the primary key |
| [Unique index](unique-index-ysql) | Yes | Yes | Set one or many columns to be unique |
| [Multi-column index](secondary-indexes-ysql/#multi-column-index) | Yes | Yes | Index on multiple columns for faster scan with lesser rows |
| [Partial index](partial-index-ysql) | Yes | Yes | Indexes that apply to only some rows of the table |
| [Covering index](covering-index-ysql) | Yes | Yes | Store other columns in the index for faster retrieval |
| [Expression index](expression-index-ysql) | Yes | No | Index based on a functional operation on columns |
| [GIN index](gin) | Partial | No | Generalized inverted index for fast text search |
| GIST Index | No | No | For spatial search. Tracked - {{<issue 1337>}} |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Primary keys"
    body="Explore the use of primary keys in YSQL and YCQL."
    href="primary-key-ysql/"
    icon="fa-solid fa-bars">}}

  {{<index/item
    title="Foreign keys"
    body="Explore the use of foreign keys associated with primary keys in YSQL."
    href="foreign-key-ysql/"
    icon="fa-solid fa-list-ul">}}

  {{<index/item
    title="Secondary and multi-column indexes"
    body="Explore indexes to optimize your database performance with examples."
    href="secondary-indexes/"
    icon="fa-solid fa-list-ol">}}

  {{<index/item
    title="Unique indexes"
    body="Explore unique indexes in YSQL and YCQL with examples."
    href="unique-index-ysql/"
    icon="fa-solid fa-bars-staggered">}}

  {{<index/item
    title="Partial indexes"
    body="Explore partial indexes in YSQL and YCQL with examples."
    href="partial-index-ysql/"
    icon="fa-solid fa-list-check">}}

  {{<index/item
    title="Expression indexes"
    body="Explore Expression indexes in YSQL with examples."
    href="expression-index-ysql/"
    icon="fa-solid fa-percent">}}

   {{<index/item
    title="Covering indexes"
    body="Explore Covering indexes in YSQL with examples."
    href="covering-index-ysql/"
    icon="fa-solid fa-table-list">}}

  {{<index/item
    title="GIN indexes"
    body="Use GIN indexes in YSQL to run efficient queries."
    href="gin/"
    icon="fa-solid fa-folder-tree">}}

  {{<index/item
    title="Other constraints"
    body="Explore CHECK, UNIQUE, and NOT NULL constraints to optimize your database performance."
    href="other-constraints/"
    icon="/images/section_icons/develop/learn.png">}}

{{</index/block>}}