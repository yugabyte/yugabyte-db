---
title: Manual review guideline for YugabyteDB Voyager
headerTitle: Manual review guideline
linkTitle: Manual review guideline
image: /images/section_icons/develop/learn.png
headcontent: What to watch out for when migrating data using YugabyteDB Voyager
description: What to watch out for when migrating data using YugabyteDB Voyager.
type: indexpage
showRightNav: true
menu:
  preview_yugabyte-voyager:
    identifier: known-issues
    parent: yugabytedb-voyager
    weight: 104
---

Review the unsupported features, limitations, and implement suggested workarounds to successfully migrate data from MySQL, Oracle, or PostgreSQL to YugabyteDB.

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :------ | :------------------------ | :----------- |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |
| USERS/GRANTS | Voyager does not support migrating the USERS and GRANTS from the source database to the target cluster. |
| Unsupported data types | Data migration is unsupported for some data types, such as BLOB and XML. For others such as ANY and BFile, both schema and data migration is unsupported. Refer to [datatype mapping](../reference/datatype-mapping-oracle/) for the detailed list of data types. | |
| Unsupported PostgreSQL features | Yugabyte currently doesn't support the PostgreSQL features listed in [PostgreSQL compatibility](../../explore/ysql-language-features/postgresql-compatibility/#unsupported-postgresql-features). If such schema clauses are encountered, Voyager results in an error. | |

## Manual review

{{<index/block>}}

  {{<index/item
    title="General"
    body="Explore workarounds for limitations associated with MySQL, PostgreSQL, and Oracle source databases."
    href="general-issues/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="MySQL"
    body="Explore workarounds for limitations associated with MySQL as the source database."
    href="mysql/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="PostgreSQL"
    body="Explore workarounds for limitations associated with PostgreSQL as the source database."
    href="postgresql/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="Oracle"
    body="Explore workarounds for limitations associated with Oracle as the source database."
    href="oracle/"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="MySQL and Oracle"
    body="Explore workarounds for limitations associated with MySQL and Oracle source databases."
    href="mysql-oracle/"
    icon="/images/section_icons/architecture/concepts.png">}}

{{</index/block>}}
