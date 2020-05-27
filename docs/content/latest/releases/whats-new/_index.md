---
title: What's new in 2.1.7
headerTitle: What's new in 2.1.7
linkTitle: What's new in 2.1.7
description: Enhancements, changes, and resolved issues in the latest YugabyteDB release.
headcontent: Features, enhancements, and resolved issues in the latest release.
image: /images/section_icons/quick_start/install.png
aliases:
  - /latest/releases/
section: RELEASES
menu:
  latest:
    identifier: whats-new
    weight: 2589 
---

**Released:** May 26, 2020 (2.1.7.0-b19). ???

**New to YugabyteDB?** Follow [Quick start](../../quick-start/) to get started and running in less than five minutes.

**Looking for earlier releases?** History of earlier releases is available [here](../earlier-releases/).  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.7.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp; 
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.7.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.1.7.0-b17 ???
```

## YSQL


## YCQL

- Update Cassandra Java driver version to `3.8.0-yb-4` and adds support for [`guava`](https://github.com/google/guava) 26 or later. The latest release of the driver is available in the [Yugabyte `cassandra-java-driver` repository](https://github.com/yugabyte/cassandra-java-driver/releases). [#3897](https://github.com/yugabyte/yugabyte-db/issues/3897)

## System improvements

- Improve load balancing by creating a global count of tablets starting across all tables before issuing AddReplica requests to prevent exceeding the maximum number of tablets being remote bootstrapped. [#4053](https://github.com/yugabyte/yugabyte-db/issues/4053)
- [Colocation] During load balancing operations, load balance each colocated tablet once. This fix removes unnecessary load balancing for every user table sharing that table and the parent table.
- Redirect the master UI to the master leader UI without failing when one master is down. [#4442](https://github.com/yugabyte/yugabyte-db/issues/4442) and [#3869](https://github.com/yugabyte/yugabyte-db/issues/3869)
- Avoid race in `change_metadata_operation`. Use atomic<P*> to avoid race between
`Finish()` and `ToString` from updating or accessing request. [#3912](https://github.com/yugabyte/yugabyte-db/issues/3912)
- Refactor `RaftGroupMetadata` to avoid keeping unnecessary `TableInfo` objects in memory.

## Yugabyte Platform

- Improve latency tracking by splitting overall operation metrics into individual rows for each API. [#3825](https://github.com/yugabyte/yugabyte-db/issues/3825)
  - YCQL and YEDIS metrics include `ops`, `avg latency`, and `P99 latency`.
  - YSQL metrics include only `ops` and `avg latency`.
- When configuration flags are deleted using the YugabyteDB Admin Console, they are also removed from `server.conf` file and the server restarts. [#4341](https://github.com/yugabyte/yugabyte-db/issues/4341)

{{< note title="Note" >}}

Prior to 2.0, YSQL was still in beta. As a result, 2.0 release includes a backward incompatible file format change for YSQL. This means that if you have an existing cluster running releases older than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from old cluster and import into a new 2.0+ cluster is needed for using existing data.

{{< /note >}}
