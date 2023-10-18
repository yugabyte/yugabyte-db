---
title: Release versioning
headerTitle: Release versioning
linkTitle: Release versioning
description: Explains the new release versioning convention for preview and stable releases.
aliases:
  - /stable/releases/versioning/
  - /v2.16/releases/versioning/
  - /v2.14/releases/versioning/
  - /v2.12/releases/versioning/
  - /v2.8/releases/versioning/
type: docs
---

Starting with v2.16, YugabyteDB follows a new release versioning convention for stable (production-ready) and preview (development) releases. There are now three release types: long-term stable (LTS), standard-term stable (STS), and preview. The following sections cover the new release versioning convention and describe the release series types.

## Release versioning convention

YugabyteDB follows the [semantic versioning (SemVer)](https://semver.org) convention for numbering release versions, modified slightly to use even and odd minor releases denote stable and development releases, respectively. Release versions follow the versioning format of `MAJOR.MINOR.PATCH.REVISION`, where non-negative integers are used for:

- `MAJOR` — Includes substantial changes.
- `MINOR` — Incremented when new features and changes are introduced.
  - `EVEN` — Stable minor release, intended for production deployments.
  - `ODD` — Preview minor release, intended for development and testing.
- `PATCH` — Patches in a stable release (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility. For patches in the preview release series (`MAJOR.ODD.PATCH`), new features and changes are introduced that might break backward compatibility.
- `REVISION` - Occasionally, a revision is required to address an issue without delay. Most releases are a `.0` revision level.

Examples are included in the relevant sections below.

## Stable releases

Releases in LTS (long-term stable) and STS (standard-term stable) release series, denoted by `MAJOR.EVEN` versioning, introduce fully tested new features and changes added after the last stable release. A stable release series is based on the preceding preview release series. For example, the v2.16 STS release series is based on the v2.15 preview release series.

Patch and revision releases in a stable release series (`MAJOR.EVEN`) include bug fixes and revisions that do not break backward compatibility.

{{< note title="Important" >}}

- Yugabyte supports *production deployments* on stable YugabyteDB releases and upgrades to newer stable releases. For a list of supported stable releases, see [Current supported releases](../../releases/#current-supported-releases).
- For recommendations on which version to use for development and testing, see [Recommended release series for projects](../../releases/#recommended-release-series-for-projects).

{{< /note >}}

## Preview releases

Releases in the preview release series, denoted by `MAJOR.ODD` versioning, are under active development and incrementally introduce new features and changes, and are intended for development, testing, and proof-of-concept projects. The v2.13 preview release series became the basis for the v2.14 LTS release series. **The current preview version is {{< yb-version version="preview" format="">}}**.

Patch releases in the preview release series (`MAJOR.ODD.PATCH`) introduce new features, enhancements, and fixes.

{{< note title="Note" >}}

- The preview release series is not supported for production deployments.
- There is currently no migration path from a preview release to a stable release.

{{< /note >}}
