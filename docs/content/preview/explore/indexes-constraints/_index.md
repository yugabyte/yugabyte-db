---
title: Indexes and constraints in YugabyteDB
headerTitle: Indexes and constraints
linkTitle: Indexes and constraints
description: Using indexes and constraints in YugabyteDB
headcontent: Explore indexes and constraints in YSQL and YCQL
image: /images/section_icons/explore/monitoring.png
menu:
  preview:
    identifier: explore-indexes-constraints
    parent: explore
    weight: 245
type: indexpage
---

In YugabyteDB, indexes are stored in a distributed manner just like tables. They are split into tablets and replicated. Updates to indexes are transactional which means that row updates and the corresponding index updates happen as one single transaction. Just like tables, they are stored in [LSM](https://en.wikipedia.org/wiki/Log-structured_merge-tree) format as compared to the B-tree structure in Postgres.

In YSQL API, YugabyteDB supports most of the Postgres semantics and in the YCQL API, most of Cassandra index semantics are supported along with other improvements. Let us look at the different types of indexes and their support across the two APIs.

## Types of indexes

|                   Type                    |  YSQL   | YCQL |                         Info                         |
| ----------------------------------------- | ------- | ---- | ---------------------------------------------------- |
| [Primary Key](primary-key-ysql)           | Yes     | Yes  | Unique Key that indentifies the row                  |
| [Foreign Key](foreign-key-ysql)           | Yes     | No   | Link to a column in another table                    |
| [Secondary Index](secondary-indexes)      | Yes     | Yes  | Index on columns other than the primary key          |
| [Unique Index](unique-index-ysql)         | Yes     | Yes  | Set one or many columns to be unique                 |
| [Partial Index](partial-index-ysql)       | Yes     | Yes  | Indexes that apply to only some rows of the table    |
| [Covering Index](covering-index-ysql)     | Yes     | Yes  | Store other columns in the indx for faster retrieval |
| [Expression Index](expression-index-ysql) | Yes     | No   | Index based on a functional operation on columns     |
| [GIN Index](gin)                          | Partial | No   | Generalized inverted index for fast text search      |
| GIST Index                                | No      | No   | For spatial search. Tracked - {{<issue 1337>}}       |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Primary keys"
    body="Explore the use of Primary keys in YSQL and YCQL with examples."
    href="primary-key-ysql/"
    icon="fa-solid fa-bars">}}

  {{<index/item
    title="Foreign keys"
    body="Explore the use of Foreign keys associated with Primary keys in YSQL."
    href="foreign-key-ysql/"
    icon="fa-solid fa-list-ul">}}

  {{<index/item
    title="Secondary indexes"
    body="Explore Indexes to optimize your database performance."
    href="secondary-indexes/"
    icon="fa-solid fa-list-ol">}}

  {{<index/item
    title="Unique indexes"
    body="Explore Unique indexes in YSQL and YCQL with examples."
    href="unique-index-ysql/"
    icon="fa-solid fa-bars-staggered">}}

  {{<index/item
    title="Partial indexes"
    body="Explore Partial indexes in YSQL and YCQL with examples."
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
