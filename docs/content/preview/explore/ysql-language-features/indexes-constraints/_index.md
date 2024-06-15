---
title: Indexes and constraints in YugabyteDB YSQL
headerTitle: Indexes and constraints
linkTitle: Indexes and constraints
description: Using indexes and constraints in YugabyteDB YSQL
headcontent: Explore indexes and constraints in YSQL
image: /images/section_icons/explore/monitoring.png
menu:
  preview:
    identifier: explore-indexes-constraints-ysql
    parent: explore-ysql-language-features
    weight: 245
type: indexpage
---

As with tables, indexes in YugabyteDB are stored in a distributed manner - that is, they are split into tablets and replicated. Updates to indexes are transactional, which means that row updates and the corresponding index updates occur as a single transaction. Similar to tables, they are stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format, as opposed to the [B-tree](https://www.postgresql.org/docs/current/btree-implementation.html#BTREE-STRUCTURE) structure used by indexes in PostgreSQL.

{{<note>}}
The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded/distributed. Indexes are not colocated with the base table.
{{</note>}}

YugabyteDB supports most of the PostgreSQL index semantics in the [YSQL API](../../../api/ysql/).

The following table lists different types of indexes and their support in YSQL.

|                | Type | Description  |
| :------------- | :--- | :--- |
| {{<icon/yes>}} | [Primary key](primary-key-ysql/) | Unique key that identifies the row |
| {{<icon/yes>}} | [Foreign key](foreign-key-ysql/) | Link to a column in another table |
| {{<icon/yes>}} | [Secondary index](secondary-indexes-ysql/) | Index on columns other than the primary key |
| {{<icon/yes>}} | [Unique index](unique-index-ysql/) | Set one or many columns to be unique |
| {{<icon/yes>}} | [Multi-column index](secondary-indexes-ysql/#multi-column-index) | Index on multiple columns for faster scan with lesser rows |
| {{<icon/yes>}} | [Partial index](partial-index-ysql/) | Indexes that apply to only some rows of the table |
| {{<icon/yes>}} | [Covering index](covering-index-ysql/) | Store other columns in the index for faster retrieval |
| {{<icon/yes>}} | [Expression index](expression-index-ysql/) | Index based on a functional operation on columns |
| {{<icon/partial>}} | [GIN index](gin) | Generalized inverted index for fast text search |
| {{<icon/no>}}  | GIST Index | For spatial search. Tracked - {{<issue 1337>}} |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Primary keys"
    body="Explore the use of primary keys in YSQL."
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
    href="secondary-indexes-ysql/"
    icon="fa-solid fa-list-ol">}}

  {{<index/item
    title="Unique indexes"
    body="Explore unique indexes in YSQL with examples."
    href="unique-index-ysql/"
    icon="fa-solid fa-bars-staggered">}}

  {{<index/item
    title="Partial indexes"
    body="Explore partial indexes in YSQL with examples."
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

  {{<index/item
    title="Index backfill"
    body="Understand how you can create indexes without affecting ongoing queries."
    href="index-backfill/"
    icon="/images/section_icons/develop/learn.png">}}

{{</index/block>}}
