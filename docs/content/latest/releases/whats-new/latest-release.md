---
title: What's new in the v2.9 latest release series
headerTitle: What's new in the v2.9 latest release series
linkTitle: v2.9 (latest)
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

{{< tip title="Kubernetes upgrades">}}

To upgrade a pre-version 2.9.0.0 Yugabyte Platform or universe instance deployed on Kubernetes that **did not** specify a storage class override, you need to override the storage class Helm chart value (which is now "", the empty string) and set it to the previous value, "standard".

For Yugabyte Platform, the class is `yugaware.storageClass`. For YugabyteDB, the classes are `storage.master.storageClass` and `storage.tserver.storageClass`.

{{< /tip >}}

## v2.9.0.0 - August 26, 2021

Version 2.9 introduces many new features and refinements. To learn more, check out the [Announcing YugabyteDB 2.9: Pushing the Boundaries of Relational Databases](https://blog.yugabyte.com/announcing-yugabytedb-2-9/) blog post.

Yugabyte release 2.9 builds on our work in the 2.7 series, which fed into the 2.6 stable release.

**Build:** `2.9.0.0-b4`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.9.0.0-b4
```

### New Features

(Refer to the [release announcement](https://blog.yugabyte.com/announcing-yugabytedb-2-9/) for new-feature details for this release!)

### Improvements

#### Yugabyte Platform

* [[3452](https://github.com/yugabyte/yugabyte-db/issues/3452)] [Platform] Allow TLS encryption to be enabled on existing universes
* [[3452](https://github.com/yugabyte/yugabyte-db/issues/3452)] [Platform] Wrapper API handling both TLS Toggle and Cert Rotation
* [[8296](https://github.com/yugabyte/yugabyte-db/issues/8296)] [Platform] Ability to stop backups from admin console. (#9310)
* [[8489](https://github.com/yugabyte/yugabyte-db/issues/8489)] [Platform] Update CertsRotate upgrade task to support rootCA rotation
* [[8637](https://github.com/yugabyte/yugabyte-db/issues/8637)] [Platform]: Adding APIs to schedule External user-defined scripts.
* [[9236](https://github.com/yugabyte/yugabyte-db/issues/9236)] Replace cron jobs with systemd services for yb-master, yb-tserver, clean_cores, and zip_purge_yb_logs.
* [[9237](https://github.com/yugabyte/yugabyte-db/issues/9237)] Upgrade cron based universes to systemd universes.
* [[9238](https://github.com/yugabyte/yugabyte-db/issues/9238)] Changed AWS Default storage type from GP2 to GP3
* [[9272](https://github.com/yugabyte/yugabyte-db/issues/9272)] Add connection strings for JDBC, YSQL and YCQL in connect Dialog (#9473)
* [[9302](https://github.com/yugabyte/yugabyte-db/issues/9302)] Adding custom machine image option for GCP
* [[9325](https://github.com/yugabyte/yugabyte-db/issues/9325)] updated aws pricing data by running aws utils.py script. pricing data now includes t2 data. t2 instances can now be used when launching universes.
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Add Snappy and LZ4 traffic compression algorithms
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Implement network traffic compression
* [[9372](https://github.com/yugabyte/yugabyte-db/issues/9372)] Add RPC call metrics
* [[9497](https://github.com/yugabyte/yugabyte-db/issues/9497)] [Platform] Add more regions to GCP metadata
* [PLAT-1501] [Platform] Support downloading YB tarball directly on the DB nodes
* [PLAT-1522] [Platform] Support downloading releases directly from GCS
* AWS disk modification wait method updated to return faster

#### Database

### Bug Fixes

#### Yugabyte Platform

* [[7456](https://github.com/yugabyte/yugabyte-db/issues/7456)] [Platform] Add Labels to GCP Instances and disks
* [[8152](https://github.com/yugabyte/yugabyte-db/issues/8152)] [Platform] Enforce configured password policy (#9210)
* [[8798](https://github.com/yugabyte/yugabyte-db/issues/8798)] Add version numbers to UpgradeUniverse task info
* [[8950](https://github.com/yugabyte/yugabyte-db/issues/8950)] [Platform] Use matching helm chart version to operate on db k8s pods
* [[9098](https://github.com/yugabyte/yugabyte-db/issues/9098)] [Platform] Fix sample apps command syntax for k8s universes
* [[9213](https://github.com/yugabyte/yugabyte-db/issues/9213)] [Platform] [UI] Leading or trailing spaces are not removed from username field on login console
* [[9245](https://github.com/yugabyte/yugabyte-db/issues/9245)] [Platform] Add empty check before adding tags
* [[9245](https://github.com/yugabyte/yugabyte-db/issues/9245)] [Platform] Tag AWS Volumes and Network Interfaces
* [[9260](https://github.com/yugabyte/yugabyte-db/issues/9260)] [Platform] Fix Health Check UI not rendering
* [[9278](https://github.com/yugabyte/yugabyte-db/issues/9278)] changing kubernetes provider config labels
* [[9331](https://github.com/yugabyte/yugabyte-db/issues/9331)] [Platform] Allow editing "Configuration Name" for backup storage provider without security credentials
* [[9363](https://github.com/yugabyte/yugabyte-db/issues/9363)] fixing metric graph line labels
* [[9363](https://github.com/yugabyte/yugabyte-db/issues/9363)] removing TServer references from graph titles
* [[9365](https://github.com/yugabyte/yugabyte-db/issues/9365)] [Platform] Optimise CertificateInfo.getAll by populating universe details in batch rather than individually
* [[9377](https://github.com/yugabyte/yugabyte-db/issues/9377)] Improving system load graph labels
* [[9403](https://github.com/yugabyte/yugabyte-db/issues/9403)] [UI] Add submit type to submit button in YBModal
* [[9403](https://github.com/yugabyte/yugabyte-db/issues/9403)] Fix form submission causing refresh for confirmation modal
* [[9425](https://github.com/yugabyte/yugabyte-db/issues/9425)] [Platform] Slow Query Calls using custom username/password
* [[9580](https://github.com/yugabyte/yugabyte-db/issues/9580)] [Platform]: Added a restore_time field in backup restore flow for AWS portal only using Feature Flags.
* [[9628](https://github.com/yugabyte/yugabyte-db/issues/9628)] [Platform] Increase wait for yum lockfile to be released during preprovisioning
* [[9635](https://github.com/yugabyte/yugabyte-db/issues/9635)] [Platform] "None" in zone field observed on tserver status page in platform
* [[9662](https://github.com/yugabyte/yugabyte-db/issues/9662)] [[9417](https://github.com/yugabyte/yugabyte-db/issues/9417)] Set enable_log_retention_by_op_idx to true by default and bump update_metrics_interval_ms to 15000
* [[9692](https://github.com/yugabyte/yugabyte-db/issues/9692)] Fix initialization of async cluster form values for existing universes without read-replica
* [[9713](https://github.com/yugabyte/yugabyte-db/issues/9713)] [Platform] Do not perform version checks if HA is not set
* [[9854](https://github.com/yugabyte/yugabyte-db/issues/9854)] Disable drive aware LB logic by default
* [PLAT-1520] [Platform] Stop displaying external script schedule among Backup Schedules
* [PLAT-1524] [Platform] Fix password policy validation
* [PLAT-1540] [Platform] Make health check use both possible client to node CA cert location
* [PLAT-1559] [Platform] Stop the external script scheduler if the universe is not present
* [Platform] Disable "Pause Universe" operation for Read-Only users (#9308)
* [Platform] Extends ToggleTLS with ClientRootCA and General Certificates Refactor
* Delete associated certificates while deleting universe
* Make backup configuration name unique for each customer

#### Database

### Known Issues

#### Yugabyte Platform

N/A

#### Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
