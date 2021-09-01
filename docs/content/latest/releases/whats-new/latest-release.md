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


{{< note title="Kubernetes upgrade">}}
To upgrade a pre-version 2.9.0.0 Yugabyte Platform or universe instance deployed on Kubernetes that **did not** specify a storage class override, you need to override the storage class Helm chart value (which is now "", the empty string) and set it to the previous value, "standard".

For Yugabyte Platform, the class is `yugaware.storageClass`. For YugabyteDB, the classes are `storage.master.storageClass` and `storage.tserver.storageClass`.
{{< /note >}}

## v2.9.0 - August 31, 2021

Version 2.9 introduces many new features and refinements. To learn more, check out the [Announcing YugabyteDB 2.9: Pushing the Boundaries of Relational Databases](https://blog.yugabyte.com/announcing-yugabytedb-2-9/) blog post.

Yugabyte release 2.9.0 builds on our work in the 2.7 series, which fed into the 2.6 stable release.

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


#### Database


### Bug Fixes

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
