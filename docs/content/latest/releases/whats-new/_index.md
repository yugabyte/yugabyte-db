---
title: What's new in 2.1.6
headerTitle: What's new in 2.1.6
linkTitle: What's new in 2.1.6
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

**Released:** May 8, 2020 (2.1.6.0-b17).

**New to YugabyteDB?** Follow [Quick start](../../quick-start/) to get started and running in less than five minutes.

**Looking for earlier releases?** History of earlier releases is available [here](../earlier-releases/).  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.6.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp; 
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.6.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.1.6.0-b17
```

## Yugabyte Platform

- When performing a full move or add node on a universe that has a yb-master, the `server.conf` file is now being updated with the new `master_addresses`. [#4242](https://github.com/yugabyte/yugabyte-db/issues/4242)

### YugabyteDB Admin Console

- In the **Backups** tab, individual YSQL tables can no longer be selected. [#3848](https://github.com/yugabyte/yugabyte-db/issues/3848)
- In the **Metrics** view, transactions have been added to the YSQL and YCQL operations charts. [#3827](https://github.com/yugabyte/yugabyte-db/issues/3827)
- **Create Read Replica** and **Edit Read Replica** pages are no longer in beta. [#4313](https://github.com/yugabyte/yugabyte-db/issues/4313)
- In the **Certificates** page, you can now download certificates. [#3985](https://github.com/yugabyte/yugabyte-db/issues/3985)

{{< note title="Note" >}}

Prior to 2.0, YSQL was still in beta. As a result, 2.0 release includes a backward incompatible file format change for YSQL. This means that if you have an existing cluster running releases older than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from old cluster and import into a new 2.0+ cluster is needed for using existing data.

{{< /note >}}
