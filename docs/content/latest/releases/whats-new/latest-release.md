---
title: What's new in the v2.7 latest release series
headerTitle: What's new in the v2.7 latest release series
linkTitle: v2.7 (latest)
description: Enhancements, changes, and resolved issues in the latest release series.
headcontent: Features, enhancements, and resolved issues in the latest release series.
image: /images/section_icons/quick_start/install.png
aliases:
  - /latest/releases/
menu:
  latest:
    parent: whats-new
    identifier: latest-release
    weight: 2585
isTocNested: true
showAsideToc: true 
---

## v2.7.0 - Apr 19, 2021

STEVE V: need bug fixes in master branch from time of 2.5.3.0 release build through 2.7.0.0

Yugabyte release 2.7.0 builds on our work in the 2.5 series, which fed into the 2.4 stable release. With release 2.7.0, we're planning development on a number of [new features](#new-features), as well as refinements to existing functionality.

**Build:** `2.7.0.0-b17`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.7.0.0-b17
```

### New Features

#### Yugabyte Platform

* Yugabyte Platform High Availability is now available in BETA. With this feature, you can deploy multiple platforms in an active-standby cluster, ensuring you can always monitor and manage your platform-managed universes regardless of outages to any particular platform in the HA cluster.
* [[7225](https://github.com/yugabyte/yugabyte-db/issues/7225)] [[7228](https://github.com/yugabyte/yugabyte-db/issues/7228)] [UI] Enable the OpenShift tab, marked as beta

#### Core Database

* [[2717](https://github.com/yugabyte/yugabyte-db/issues/2717)] YSQL: Support ALTER FUNCTION command
* [[6985](https://github.com/yugabyte/yugabyte-db/issues/6985)] YSQL: Add simple UNIQUE column

### Improvements

#### Yugabyte Platform

* [[5628](https://github.com/yugabyte/yugabyte-db/issues/5628)] [Azure] Support regions with no availability zones
* [[5807](https://github.com/yugabyte/yugabyte-db/issues/5807)] Add toggle to show deleted backups
* [[6373](https://github.com/yugabyte/yugabyte-db/issues/6373)] Pause/Resume universe
* [[6962](https://github.com/yugabyte/yugabyte-db/issues/6962)] Expose endpoint for downloading log files
* [[7475](https://github.com/yugabyte/yugabyte-db/issues/7475)] Use more recent CentOS-7 base image for GCP universe VMs

#### Core Database

* [[7121](https://github.com/yugabyte/yugabyte-db/issues/7121)] Extend yb-admin restore_snapshot to use a custom time
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Introduce snapshot schedule
* [[7366](https://github.com/yugabyte/yugabyte-db/issues/7366)] YSQL: Allow getting current SQL query in pggate for debug logging
* [[7418](https://github.com/yugabyte/yugabyte-db/issues/7418)] [[7463](https://github.com/yugabyte/yugabyte-db/issues/7463)] YSQL: Import the 'force' option for the Drop Database command

### Known Issues

#### Yugabyte Platform

* Azure IaaS orchestration
    * No support for regions with zero Availability Zones(AZs) (#5628)

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
