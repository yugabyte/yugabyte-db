---
title: Versioning (stable and latest releases)
headerTitle: YugabyteDB Versioning (stable and latest releases)
linkTitle: Versioning (stable and latest)
description: Explains the new release versioning approach for latest and stable releases.
section: RELEASES
menu:
  latest:
    identifier: whats-new
    weight: 2588 
---

Starting with version 2.2.0.0, YugabyteDB release versions use semantic versioning, but even and odd minor releases denote stable and development releases as described below.

YugabyteDB release versions follow the semantic versioning format of `MAJOR.MINOR.PATCH`, where:

- `MAJOR`
- `MINOR`
  - `EVEN`: stable release
  - `ODD`: development release
- `PATCH`: revision
- `HOTFIX`(?)

For example, in YugabyteDB version `2.2.2.1`, `2.2` refers to a stable
release series and `.2.1` refers to the revision.

## Development releases

Development releases for YugabyteDB are based on the last stable release, but introduce new features that may break backwards compatibility.

The If `MINOR` is an odd integer, `MAJOR.MINOR-ODD` denotes a development release series.

For example, `2.3` is the current development release series; the next development release series will be `2.5`.

{{< note title="Note" >}}

Development releases should only be used for testing and development, and are note supported for production deployments.

{{< /note >}}

Changes in the release series (e.g. `4.0` to `4.2`) generally mark the introduction of new features that may break backwards compatibility.

### Documentation for development releases

Documentation for the latest development release series are a work-in-progress. The goal is to provide documentation for new features as soon as possible, to help you develop and test using the new features.

## Stable releases

{{< note title="Important" >}}

For production deployments, upgrade to the latest *stable* version.

{{< /note >}}

- If `Y` is an even integer, `X.Y` refers to a stable release series; for example,
  `4.0` release series and `4.2` release series. Release series are
  **stable** and suitable for production.

### Documentation for stable releases

asdfa


## Major releases

Major releases of YugabyteDB are denoted when `MAJOR` increments by one. For example, `2.0` and `3.0` are major releases.
