---
title: Release versioning
headerTitle: Release versioning and feature availability
linkTitle: Versioning
description: Explains the new release versioning convention for preview and stable releases.
aliases:
  - /stable/releases/versioning/
  - /v2.16/releases/versioning/
  - /v2.14/releases/versioning/
  - /v2.12/releases/versioning/
  - /v2.8/releases/versioning/
type: docs
---

Starting with v2.16, YugabyteDB follows a new release versioning convention for stable (production-ready) and preview (development) releases. There are three release types: long-term support (LTS), standard-term support (STS), and preview.

Individual features may also be made available as tech previews or early access, which have different documentation and support standards than general availability features.

## Release versioning convention

YugabyteDB follows the [semantic versioning (SemVer)](https://semver.org) convention for numbering release versions, modified slightly to use even and odd minor releases denote stable and development releases, respectively. Release versions follow the versioning format of `MAJOR.MINOR.PATCH.REVISION`, where non-negative integers are used for:

- `MAJOR` — Includes substantial changes.
- `MINOR` — Incremented when new features and changes are introduced.
  - `EVEN` — Stable minor release, intended for production deployments.
  - `ODD` — Preview minor release, intended for development and testing.
- `PATCH` — Patches in a stable release (`MAJOR.EVEN.PATCH`) include bug fixes and revisions that do not break backward compatibility. For patches in the preview release series (`MAJOR.ODD.PATCH`), new features and changes are introduced that might break backward compatibility.
- `REVISION` - Occasionally, a revision is required to address an issue without delay. Most releases are a `.0` revision level.

Examples are included in the relevant sections below.

### Stable releases

Releases in LTS (long-term support) and STS (standard-term support) release series, denoted by `MAJOR.EVEN` versioning, introduce fully tested new features and changes added after the last stable release. A stable release series is based on the preceding preview release series. For example, the v2.16 STS release series is based on the v2.15 preview release series.

Features in stable releases are considered to be {{<badge/ga>}} unless marked otherwise.

Patch and revision releases in a stable release series (`MAJOR.EVEN`) include bug fixes and revisions that do not break backward compatibility.

{{< note title="Important" >}}

- Yugabyte supports *production deployments* on stable YugabyteDB releases and upgrades to newer stable releases. For a list of releases and their support timelines, see [YugabyteDB releases](../ybdb-releases/) and [YugabyteDB Anywhere releases](../yba-releases/).
- For recommendations on which version to use for development and testing, see [Recommended release series for projects](../../releases/#recommended-release-series-for-projects).

{{< /note >}}

### Preview releases

Releases in the preview release series, denoted by `MAJOR.ODD` versioning, are under active development and incrementally introduce new features and changes, and are intended for development, testing, and proof-of-concept projects. The v2.13 preview release series became the basis for the v2.14 LTS release series. **The current preview version is {{< yb-version version="preview" format="">}}**.

Features in preview releases are considered to be {{<badge/tp>}} unless marked otherwise.

Patch releases in the preview release series (`MAJOR.ODD.PATCH`) introduce new features, enhancements, and fixes.

{{< note title="Note" >}}

- The preview release series is not supported for production deployments.
- There is currently no migration path from a preview release to a stable release.

{{< /note >}}

## Feature availability

YugabyteDB features are made available as one of the following:

- {{<badge/tp>}} Tech Preview
- {{<badge/ea>}} Early Access
- {{<badge/ga>}} General Availability

Changes for EA and GA are always reported in the Release Notes.

| Description | {{<badge/tp>}} | {{<badge/ea>}} | {{<badge/ga>}} | Deprecated |
| :--- | :--- | :--- | :--- | :--- |
| Contact with Product Team | Y | Recommended | N | N/A |
| Changes | Subject to change | Backwards compatible | Backwards compatible | N/A |
| Support | N | Y | Y | N |
| SLA | N | Y | Y | Y |
| SLA (YBM) | N | N | Y | Y |
| In Release Notes | Y | Y | Y | Y |
| Availability | By invitation or request | Y | Y | Y |
| Documentation | Limited | Y | Y | N/A |

### Tech Preview (TP)

Features in Tech Preview (TP) are managed and supported by the Product Team and have been internally validated for a set of use cases. Minimal documentation is directly provided to customers and is normally not publicly hosted.

TP features aren't supported by Customer Support and may change at any time during the preview.

If you are interested in a TP feature, contact {{% support-general %}}.

Participating customers agree to provide feedback to the Product Team. However, the timeline for addressing specific areas of feedback (including bugs) is at the discretion of Yugabyte.

Documentation (if provided) for TP features is marked as such.

### Early Access (EA)

A feature in Early Access (EA) is new or enhanced functionality made available for you to use.

Code is well tested. Typically these features are not enabled by default. Enabling the feature is considered safe.

Support for the overall feature will not be dropped, though details may change in a subsequent GA release.

For production use cases, it is recommended to validate the use case with the Yugabyte Product team. Refer to product documentation for specific limitations.

Any bug fixes or improvements are managed and fixed with the same timeline and processes as those in GA.

EA features in YugabyteDB Managed are not subject to the YBM SLA.

Give feedback on EA features on our [Slack community]({{<slack-invite>}}) or by filing a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues).

Documentation for EA features is marked as such.

### General Availability (GA)

A feature in General Availability (GA) is enabled by default for all customers.

GA features are supported by Yugabyte Support, and issues are addressed according to the [release support policy](../#release-support-policy).

Any feature not marked Tech Preview or Early Access should be considered GA.

### Deprecation

A feature identified as deprecated is no longer recommended for use and may be removed in the future. This means that you shouldn't use it unless you have to. Any workarounds or recommended paths forward are included in the relevant documentation, libraries, or references.

Documentation for features that have been deprecated are indicated as such.

### Minor and cosmetic changes

Minor and cosmetic changes are not marked TP or EA, including the following:

- Bug fixes that change only the incorrect behavior of the bug.
- Cosmetic changes, such as changing the label of a field in the administrator UI.
- Changes that are narrow in scope or effect, or purely additive, such as adding a new attribute.

All changes that affect customers are reported in the Release Notes.
