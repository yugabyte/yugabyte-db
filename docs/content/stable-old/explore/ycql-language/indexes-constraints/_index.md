---
title: Indexes and constraints in YugabyteDB YCQL
headerTitle: Indexes and constraints
linkTitle: Indexes and constraints
description: Using indexes and constraints in YugabyteDB YCQL
headcontent: Explore indexes and constraints in YCQL
menu:
  stable:
    identifier: explore-indexes-constraints-ycql
    parent: explore-ycql-language
    weight: 245
type: indexpage
---

As with tables, indexes in YugabyteDB are stored in a distributed manner - that is, they are split into tablets and replicated. Updates to indexes are transactional, which means that row updates and the corresponding index updates occur as a single transaction. Similar to tables, they are stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format.

{{<note>}}
The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded/distributed. Indexes are not colocated with the base table.
{{</note>}}

The [YCQL API](../../../api/ycql/) supports most of the Cassandra index semantics while incorporating other improvements.

The following table lists different types of indexes and their support in YCQL.

|                | Type | Description  |
| :------------- | :--- | :--- |
| {{<icon/yes>}} | [Primary key](primary-key-ycql/) | Unique key that identifies the row |
| {{<icon/no>}}  | Foreign key  | Link to a column in another table |
| {{<icon/yes>}} | [Secondary index](secondary-indexes-ycql/) | Index on columns other than the primary key |
| {{<icon/yes>}} | [Unique index](unique-index-ycql/) | Set one or many columns to be unique |
| {{<icon/yes>}} | [Multi-column index](secondary-indexes-ycql/#multi-column-index) | Index on multiple columns for faster scan with lesser rows |
| {{<icon/yes>}} | [Partial index](partial-index-ycql/) | Indexes that apply to only some rows of the table |
| {{<icon/yes>}} | [Covering index](covering-index-ycql/) | Store other columns in the index for faster retrieval |
| {{<icon/no>}}  | Expression index | Index based on a functional operation on columns |
| {{<icon/no>}}  | GIN index | Generalized inverted index for fast text search |
| {{<icon/no>}}  | GIST Index | For spatial search. Tracked - {{<issue 1337>}} |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Primary keys"
    body="Explore the use of primary keys in YCQL."
    href="primary-key-ycql/"
    icon="fa-thin fa-bars">}}

  {{<index/item
    title="Secondary and multi-column indexes"
    body="Explore indexes to optimize your database performance with examples."
    href="secondary-indexes-ycql/"
    icon="fa-thin fa-list-ol">}}

  {{<index/item
    title="Unique indexes"
    body="Explore unique indexes in YCQL with examples."
    href="unique-index-ycql/"
    icon="fa-thin fa-bars-staggered">}}

  {{<index/item
    title="Partial indexes"
    body="Explore partial indexes in YCQL with examples."
    href="partial-index-ycql/"
    icon="fa-thin fa-list-check">}}

   {{<index/item
    title="Covering indexes"
    body="Explore Covering indexes in YCQL with examples."
    href="covering-index-ycql/"
    icon="fa-thin fa-table-list">}}

  {{<index/item
    title="Secondary indexes with JSONB"
    body="Create covering and partial indexes with JSONB columns."
    href="secondary-indexes-with-jsonb-ycql/"
    icon="fa-thin fa-list-ol">}}

{{</index/block>}}
