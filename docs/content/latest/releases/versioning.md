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
---

**Note:** Starting with version 2.2.0.0, Yugabyte uses even/odd minor release numbering for stable and development releases.

Yugabyte follows the [semantic versioning (semver) convention](https://semver.org) for numbering release versions, modified slightly to use even and odd minor releases denote stable and development releases, respectively. Release versions follow the semantic versioning format of `MAJOR.MINOR.PATCH`, where non-negative integers are used for:

- `MAJOR` — Includes substantial, potentially backward-incompatible changes.
- `MINOR` — Incremented when new, backwards-compatible functionality is introduced.
  - `EVEN` — Stable release, intended for production deployments.
  - `ODD` — Latest release, intended for development and testing.
- `PATCH` — bug fixes and revisions that do not break backwards compatibility.

Examples follow in the relevant sections below.

## Latest release

Development releases of YugabyteDB are based on the last stable release, but introduce new features that may break backwards compatibility.

The YugabyteDB development release series is represented by and odd integer for `MINOR`. Thus, `MAJOR.ODD` denotes a development release series. YugabyteDB version `2.3` is the current development release series; the next development release series will be `2.5`.

{{< note title="Note" >}}

Development releases should only be used for testing and development, and are note supported for production deployments.

{{< /note >}}

### Documentation for development releases

Documentation in the "latest" section (default) covers the latest development release series and is a work-in-progress. Our goal is to provide documentation for new features when they are introduced, to help you develop and test the new features.

## Stable releases

Stable release series, denoted by `MAJOR.EVEN` numbers, introduce new features and changes since the last stable release and may break backwards compatibility. YugabyteDB stable release series are represented by even non-negative integers for `MINOR`. The current stable `2.2` release series will be followed by the next stable `2.4` release series.

{{< note title="Important" >}}

Production deployments are supported only for releases from a stable release series and can only be upgraded to the next stable release series. For currently supported stable releases, see [Supported stable releases](../releases-overview/#supported-stable-releases).

{{< /note >}}

## Major releases

Major releases of YugabyteDB are denoted when `MAJOR` increments by one. For example, `2.0` and `3.0` are major releases.
