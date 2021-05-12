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

## v2.7.0 - May 5, 2021

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
* **Point in time restore**:
  * [[7121](https://github.com/yugabyte/yugabyte-db/issues/7121)] Extend yb-admin restore_snapshot to use a custom time
  * [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Introduce snapshot schedule

### Improvements

#### Yugabyte Platform

* [[5535](https://github.com/yugabyte/yugabyte-db/issues/5535)] [Platform] Remove stale instance configs after cloud provider is deleted (#6975)
* [[5624](https://github.com/yugabyte/yugabyte-db/issues/5624)] Show pricing info for Azure
* [[5626](https://github.com/yugabyte/yugabyte-db/issues/5626)] Enable Hosted Zone for Azure
* [[5628](https://github.com/yugabyte/yugabyte-db/issues/5628)] [Azure] Support regions with no availability zones
* [[5807](https://github.com/yugabyte/yugabyte-db/issues/5807)] Add toggle to show deleted backups
* [[5838](https://github.com/yugabyte/yugabyte-db/issues/5838)] [Platform] Add node link to corresponding Azure portal URL
* [[5840](https://github.com/yugabyte/yugabyte-db/issues/5840)] Add user tags for Azure universes
* [[5841](https://github.com/yugabyte/yugabyte-db/issues/5841)] Show only Azure VMs that meet minimum requirements
* [[6321](https://github.com/yugabyte/yugabyte-db/issues/6321)] [Platform] Support using Shared Gallery Images when creating Azure universes
* [[6681](https://github.com/yugabyte/yugabyte-db/issues/6681)] [Platform] Show kubectl command for Kubernetes pods in Connect modal (#7506)
* [[6712](https://github.com/yugabyte/yugabyte-db/issues/6712)] Fix issue with JDK incompatibility in Java RPC client
* [[6756](https://github.com/yugabyte/yugabyte-db/issues/6756)] [Platform] Created date showing up as "Invalid Date". (#7158)
* [[7024](https://github.com/yugabyte/yugabyte-db/issues/7024)] [Platform] Unable to edit number of nodes in AZ section (#7350)
* [[7054](https://github.com/yugabyte/yugabyte-db/issues/7054)] [YW] Add conditional checks for hiding specific platform elements in non-platform mode.
* [[7372](https://github.com/yugabyte/yugabyte-db/issues/7372)] [Platform] Skip running periodic schedules when in follower mode
* [[7433](https://github.com/yugabyte/yugabyte-db/issues/7433)] [Platform] Standby instances backup time not consistent after restoring active instance
* [[7443](https://github.com/yugabyte/yugabyte-db/issues/7443)] [Platform] Fixed live query details side panel doesn’t go away after unchecked
* [[7445](https://github.com/yugabyte/yugabyte-db/issues/7445)] [Platform] Add pagination for slow queries
* [[7472](https://github.com/yugabyte/yugabyte-db/issues/7472)] [Platform] Hide the Upgrade button from info card on Pause universe. (#7504)
* [[7475](https://github.com/yugabyte/yugabyte-db/issues/7475)] Use more recent CentOS-7 base image for GCP universe VMs
* [[7493](https://github.com/yugabyte/yugabyte-db/issues/7493)] [Platform] Menu shows 0 appended to "Upgrade Software0" - Removed 0
* [[7548](https://github.com/yugabyte/yugabyte-db/issues/7548)] [Platform] Set versions for google modules in requirements.txt
* [[7549](https://github.com/yugabyte/yugabyte-db/issues/7549)] [Platform] Platform uses public IP instead of private IP to connect to Azure universes
* [[7576](https://github.com/yugabyte/yugabyte-db/issues/7576)] Ensure rsync is available on latest GCP image
* [Platform] Use more recent CentOS-7 base image for GCP universe VMs #7475
* [Platform] parsing of df output is fragile and may fail in case of "safe" error in df #7402

#### Core Database

* [[4944](https://github.com/yugabyte/yugabyte-db/issues/4944)] YSQL performance improvements in TTL-related computations
* [[5922](https://github.com/yugabyte/yugabyte-db/issues/5922)] YCQL: Improve audit logging
* [[7359](https://github.com/yugabyte/yugabyte-db/issues/7359)] YSQL: Support adding primary key to a table with tablespace
* [[7366](https://github.com/yugabyte/yugabyte-db/issues/7366)] YSQL: Allow getting current SQL query in pggate for debug logging
* [[7404](https://github.com/yugabyte/yugabyte-db/issues/7404)] YSQL: Extend CHECKPOINT to have beta noop functionality
* [[7418](https://github.com/yugabyte/yugabyte-db/issues/7418)] [[7463](https://github.com/yugabyte/yugabyte-db/issues/7463)] YSQL: Import the 'force' option for the Drop Database command
* [[7532](https://github.com/yugabyte/yugabyte-db/issues/7532)] Master performance improvements: reduce the scope of catalog manager state lock in ScopedLeaderSharedLock constructor

### Bug Fixes

#### Core Database

* [[5383](https://github.com/yugabyte/yugabyte-db/issues/5383)] YSQL bug fixes for Jepsen

### Known Issues

#### Yugabyte Platform

N/A

#### Core Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
