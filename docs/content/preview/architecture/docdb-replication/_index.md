---
title: DocDB replication layer
headerTitle: DocDB replication layer
linkTitle: Replication
description: Learn how synchronous and asynchronous replication work in DocDB, including advanced features like xCluster replication and read replicas.
image: /images/section_icons/architecture/concepts.png
headcontent: Learn how synchronous and asynchronous replication work in DocDB.
menu:
  preview:
    identifier: architecture-docdb-replication
    parent: architecture
    weight: 800
type: indexpage
---

This section describes how replication works in DocDB. The data in a DocDB table is split into tablets. By default, each tablet is synchronously replicated using the Raft algorithm across various nodes or fault domains (such as availability zones/racks/regions/cloud providers).

YugabyteDB also provides other advanced replication features. These include two forms of asynchronous replication of data:

* **xCluster** - Data is asynchronously replicated between different YugabyteDB universes - both unidirectional replication (master-slave) or bidirectional replication across two universes.
* **Read replicas** - The in-universe asynchronous replicas are called read replicas.

The YugabyteDB synchronous replication architecture is inspired by <a href="https://research.google.com/archive/spanner-osdi2012.pdf">Google Spanner</a>.

The YugabyteDB xCluster replication architecture is inspired by RDBMS databases such as Oracle, MySQL, and PostgreSQL.

<div class="row">

  {{<index/item
    title="Default synchronous replication"
    body="In-primary-cluster synchronous replication with Raft consensus."
    href="replication/"
    icon="/images/section_icons/architecture/concepts/replication.png">}}

  {{<index/item
    title="xCluster"
    body="Cross-universe asynchronous replication of data."
    href="async-replication/"
    icon="/images/section_icons/architecture/concepts/replication.png">}}

  {{<index/item
    title="Read replicas"
    body="In-universe asynchronous replicas to enable reading data that is a bit stale with lower read latencies."
    href="read-replicas/"
    icon="/images/section_icons/architecture/concepts/replication.png">}}

</div>
