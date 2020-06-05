---
title: What's new in 2.2
headerTitle: What's new in 2.2
linkTitle: What's new in 2.2
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

**Released:** June ??, 2020 (2.2.0.0-b??).

**New to YugabyteDB?** Follow [Quick start](../../quick-start/) to get started and running in less than five minutes.

**Looking for earlier releases?** History of earlier releases is available [here](../earlier-releases/).  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.2.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp; 
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.2.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.2.0.0-b??
```

## YSQL

- Content to be added.

## YCQL

- Content to be added.

## System improvements

- Content to be added.

## Yugabyte Platform

- Content to be added.

{{< note title="Note" >}}

Prior to 2.0, YSQL was still in beta. As a result, 2.0 release includes a backward incompatible file format change for YSQL. This means that if you have an existing cluster running releases older than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from old cluster and import into a new 2.0+ cluster is needed for using existing data.

{{< /note >}}
