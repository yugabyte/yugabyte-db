---
title: Release versioning
headerTitle: Release versioning
linkTitle: Release versioning
description: Explains the new release versioning convention for latest and stable releases.
section: RELEASES
menu:
  latest:
    identifier: versioning
    weight: 2586
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
- `PATCH` — Patches in a stable release (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility. For patches in the latest release series (`MAJOR.ODD.REVISION`), new features and changes are introduced that might break backward compatibility.

Examples follow in the relevant sections below.

## Stable releases

Releases within stable release series, denoted by `MAJOR.EVEN` versioning, introduce fully tested new features and changes added since the last stable release. The `2.2` stable release series will be followed by the next `2.4` stable release series.

Patch releases in a stable release series (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility.

{{< note title="Important" >}}

Yugabyte supports *production deployments* based on stable releases and can only be upgraded to the newer stable releases. For a list of supported stable releases, see [Supported and planned releases](../releases-overview/#supported-stable-releases).

{{< /note >}}

## Latest releases

Releases within the latest release series, denoted by `MAJOR.ODD` versioning, incrementally introduces new features and changes and are intended for development and testing. The latest releases are not supported for production deployments. The `2.3` latest release series will become the basis for the next `2.4` stable release series. And the next latest release series available will then be `2.5`.

Patches released in the latest release series (`MAJOR.ODD.REVISION`) can introduce new features and changes that might break backward compatibility.
