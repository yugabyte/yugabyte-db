---
title: What's new in 2.3.1
headerTitle: What's new in 2.3.1
linkTitle: What's new in 2.3.1
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

**Released:** September 15, 2020 (2.3.1.0-b15).

**New to YugabyteDB?** To get up and running in less than five minutes, follow [Quick start](../../quick-start/).

**Looking for earlier releases?** History of earlier releases is available in [Earlier releases](../earlier-releases/) section.  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.3.1.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.3.1.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.3.1.0-b15
```

## YSQL

- Fixes OOM when using COPY <table> FROM <file> to load data from a large file to a table. [#5453](https://github.com/yugabyte/yugabyte-db/issues/5453)
- Enable 2DC replication bootstrap for YSQL tables. This allows the producer to know where to start replicating from. [#5601](https://github.com/yugabyte/yugabyte-db/issues/5601)
- Fix CREATE TABLE is 4-5x slower using Docker on Mac than not using Docker. Speeds up table creation by buffering writes to postgres system tables, caching pinned objects, and significantly reducing write RPC calls. [#3503](https://github.com/yugabyte/yugabyte-db/issues/3503)

- Fixes OOM on `COPY FROM` large files when reading from a file (not using `stdin`). [#5453](https://github.com/yugabyte/yugabyte-db/issues/5453)

## YCQL

## Core database

- Quickly evict known unresponsive tablet servers from the tablet location cache. Applies only to follower reads (aka consistent prefix reads). For example, when tablet servers are not replying to RPC calls or dead — not sending heartbeats to the master for 5 minutes. This could also happen after decommissioning nodes. [#1052](https://github.com/yugabyte/yugabyte-db/issues/1052)

## Yugabyte Platform

- Add search input and data sorting to the on-premises instances table list. Click arrows next to column titles to sort. Use the Search form to search multiple columns. [#4757](https://github.com/yugabyte/yugabyte-db/issues/4757)
- Add the ability to change the user role (`Admin`, `ReadOnly`, or `BackupAdmin`) from the UI by an admin. Also, fix stale users list after creation or deletion of a user and disable **Save** buttons at Customer Profile tabs for `ReadOnly` users. [#5311](https://github.com/yugabyte/yugabyte-db/issues/5311)
- Before deleting a universe, stop the master/tserver processes and release the instance so that if started again, the processes are not still running. [#4953](https://github.com/yugabyte/yugabyte-db/issues/4953)

{{< note title="Note" >}}

Prior to version 2.0, YSQL was still in beta. As a result, the 2.0 release included a backward-incompatible file format change for YSQL. If you have an existing cluster running releases earlier than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from your existing cluster and then import into a new cluster (v2.0 or later) to use existing data.

{{< /note >}}
