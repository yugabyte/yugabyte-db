---
title: Release versioning
headerTitle: Release versioning
linkTitle: Release versioning
description: Explains the new release versioning convention for preview and stable releases.
menu:
  preview_releases:
    identifier: versioning
    parent: release-notes
    weight: 4000
type: docs
---

Starting with v2.2, Yugabyte follows a new release versioning convention for stable and preview (development) releases. The following sections cover the new release versioning convention and includes descriptions of stable and preview releases.

## Release versioning convention

Yugabyte follows the [semantic versioning (semver)](https://semver.org) convention for numbering release versions, modified slightly to use even and odd minor releases denote stable and development releases, respectively. Release versions follow the semantic versioning format of `MAJOR.MINOR.PATCH`, where non-negative integers are used for:

- `MAJOR` — Includes substantial changes.
- `MINOR` — Incremented when new features and changes are introduced.
  - `EVEN` — Stable minor release, intended for production deployments.
  - `ODD` — Preview minor release, intended for development and testing.
- `PATCH` — Patches in a stable release (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility. For patches in the preview release series (`MAJOR.ODD.PATCH`), new features and changes are introduced that might break backward compatibility.

Examples are included in the relevant sections below.

## Stable releases

Releases within stable release series, denoted by `MAJOR.EVEN` versioning, introduce fully tested new features and changes added since the last stable release. A stable release series is based on the preceding preview release series. For example, the v2.6 stable release series was based on the v2.5 preview release series. **The current stable version is {{< yb-version version="stable" format="">}}**.

Patch releases in a stable release series (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility.

{{< note title="Important" >}}

- Yugabyte supports *production deployments* on stable releases and upgrades to newer stable releases. For a list of supported stable releases, see [Current supported releases](../../releases/#current-supported-releases).
- For recommendations on which version to use for development and testing, see [Recommended release series for projects](../../releases/#recommended-release-series-for-projects).

{{< /note >}}

## Preview releases

Releases in the preview release series, denoted by `MAJOR.ODD` versioning, are under active development and incrementally introduces new features and changes and are intended for development, testing, and proof-of-concept projects. The v2.3 preview release series became the basis for the next v2.4 stable release series. The v2.6 stable release came from the v2.5 series. **The current preview version is {{< yb-version version="preview" format="">}}**.

Patch releases in the preview release series (`MAJOR.ODD.PATCH`) introduce new features, enhancements, and fixes.

{{< note title="Note" >}}

The preview release series is not supported for production deployments.

{{< /note >}}
