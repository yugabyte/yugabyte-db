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

Yugabyte uses the semantic versioning (semver) convention for specifying release versions, modified slightly to use even and odd minor releases denote stable and development releases as described below.

YugabyteDB release versions follow the semantic versioning format of `MAJOR.MINOR.PATCH`, where:

- `MAJOR`
- `MINOR`
  - `EVEN`: stable release
  - `ODD`: development release
- `PATCH`: bug fixes and revisions

For example, YugabyteDB version `2.3.0.0` denotes the current development release.

Changes in the release series (e.g. `4.0` to `4.2`) generally mark the introduction of new features that may break backwards compatibility.

## Latest release (MAJOR.ODD)

Development releases of YugabyteDB are based on the last stable release, but introduce new features that may break backwards compatibility.

The YugabyteDB development release series is represented by and odd integer for `MINOR`. Thus, `MAJOR.ODD` denotes a development release series. YugabyteDB version `2.3` is the current development release series; the next development release series will be `2.5`.

{{< note title="Note" >}}

Development releases should only be used for testing and development, and are note supported for production deployments.

{{< /note >}}

### Documentation for development releases

Documentation in the "latest" section (default) covers the latest development release series and is a work-in-progress. Our goal is to provide documentation for new features when they are introduced, to help you develop and test the new features.

## Stable releases (MAJOR.EVEN)

Stable releases of YugabyteDB are fully tested and ready for prduction deploments. Stable releases introduce new features and changes from the last development release and may break backwards compatibility.

The YugabyteDB stable release series is represented by an even integer for `MINOR`. Thus, `MAJOR.EVEN` denotes a development release series. The current stable release series for YugabyteDB is version `2.2`; the next stable release series will be `2.4`.

`MAJOR.MINOR` refers to a stable release series when `MINOR` is an odd integer. For example, `2.2` is the current stable release series and the next stable release series will be `2.4`.

{{< note title="Important" >}}

Production deployments should only use stable releases and upgrade only to the next stable release.

{{< /note >}}

## Major releases

Major releases of YugabyteDB are denoted when `MAJOR` increments by one. For example, `2.0` and `3.0` are major releases.
