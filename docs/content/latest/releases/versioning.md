---
title: Release versioning
headerTitle: Release versioning
linkTitle: Release versioning
description: Explains the new release versioning approach for latest and stable releases.
section: RELEASES
menu:
  latest:
    identifier: versioning
    weight: 2589
isTocNested: true
showAsideToc: true
---

Starting with version `2.2`, Yugabyte uses even a new release versioning convention for stable and development releases. The following sections covers the new release versioning convention and descriptions of stable releases and the latest release.

## Release versioning convention

Yugabyte follows the [semantic versioning (semver)](https://semver.org) convention for numbering release versions, modified slightly to use even and odd minor releases denote stable and development releases, respectively. Release versions follow the semantic versioning format of `MAJOR.MINOR.PATCH`, where non-negative integers are used for:

- `MAJOR` — Includes substantial changes.
- `MINOR` — Incremented when new features and changes are introduced.
  - `EVEN` — Stable minor release, intended for production deployments.
  - `ODD` — Latest minor release, intended for development and testing.
- `PATCH` — bug fixes and revisions that do not break backwards compatibility.

Examples follow in the relevant sections below.

## Stable releases

Stable release series, denoted by `MAJOR.EVEN` numbers, introduce fully tested new features and changes added since the last stable release. The current stable `2.2` release series will be followed by the next stable `2.4` release series.

{{< note title="Important" >}}

Yugabyte supports *production deployments* based on stable releases and can only be upgraded to the newer stable releases. For a list of supported stable releases, see [Supported and planned releases](../releases-overview/#supported-stable-releases).

{{< /note >}}

## Latest releases

The latest release series, denoted by a `MAJOR.ODD` number, is the current work-in-progress release series that incrementally introduces new features and changes. The latest `2.3` release series will become the next stable `2.4` release series. And the next latest release series will be `2.5`. These `MAJOR.ODD` releases are not supported for production deployments.
