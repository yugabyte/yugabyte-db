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

YugabyteDB and YugabyteDB Anywhere have three kinds of releases:

- Stable, with Long-term support (LTS)
- Stable, with Standard-term support (STS)
- Preview, with no official support

Additionally, individual features may also be designated as tech preview or early access, or generally available. These designations indicate the feature maturity level, and provide different levels of documentation and support as described in  [Feature maturity](#feature-maturity).

## Stable releases

Features in stable releases are considered to be {{<badge/ga>}} unless marked otherwise.

Stable release versions use the numbering format `YYYY.N.MAINTENANCE.PATCH` as follows:

- `YYYY.N` - Includes substantial changes, such as new features and possibly incompatible API changes. `YYYY` is the year (for example, 2024). `N` is either 1 or 2 designating either the first release of the year, or the second. Such major releases occur roughly every 6 months. Generally, one of these releases (and its derivative maintenance and patch releases) per year is designated as LTS, and the other is designated as STS.

- `MAINTENANCE` - Includes releases which occur roughly every 1-2 months.
  - For YugabyteDB, backward compatible bug fixes are included.
  - For YugabyteDB Anywhere, functionality may be added in a backward compatible manner.

- `PATCH` - Includes bug fixes and revisions that do not break backward compatibility.

On rare occasions, YugabyteDB may issue a hot fix release. Hot fix releases are for specific point issues, and usually offered only to specific customers. Hot fix releases append an additional number to the release versioning convention as `YYYY.N.MAINTENANCE.PATCH.HOTFIX`.

{{< note title="Important" >}}

- Yugabyte supports _production deployments_ on stable YugabyteDB releases and upgrades to newer stable releases. For a list of releases and their support timelines, see [YugabyteDB releases](../ybdb-releases/) and [YugabyteDB Anywhere releases](../yba-releases/).
- For recommendations on which version to use for development and testing, see [Recommended release series for projects](../../releases/#recommended-release-series-for-projects).

{{< /note >}}

## Preview releases

Features in preview releases are considered to be {{<badge/tp>}} unless marked otherwise.

Preview releases, which include features under active development, are recommended for development and testing only.

Preview releases are the basis from which stable releases are derived. That is, the code base from a preview release is branched, and then hardened to become a stable release. The v2.21 preview release series, for example became the basis for the v2024.1 STS release series.

Note that while most features in a preview release series do appear in the derivative stable release, this cannot be guaranteed; some features may remain internally _disabled_ in the derivative stable release series to allow more time for the feature to be completed.

Releases in the preview release series, denoted by `MAJOR.ODD` versioning, are under active development and incrementally (with each MINOR release) introduce new features and changes.

Preview releases use the numbering format `MAJOR.MINOR.PATCH.HOTFIX`, where non-negative integers indicate the following:

- `MAJOR` — Includes substantial changes.
- `MINOR` — Incremented when new features and changes are introduced. Currently, by convention for historical reasons, the MINOR integer is always odd; thus, successive MINOR releases increment this number by 2 (for example, 23, 25, 27, and so on.)
- `PATCH` - Patches in the preview release series (`MAJOR.ODD.PATCH`) focus on bug fixes that do not break backward compatibility. At the same time however, new features and changes may be introduced that might break backward compatibility.
- `HOTFIX` - On rare occasions, a hot fix is required to address an issue without delay.

{{< note title="Note" >}}

- The preview release series is not supported for production deployments.
- There is currently no migration path from a preview release to a stable release.

{{< /note >}}

## Feature maturity

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
| SLA (YugabyteDB Aeon) | N | N | Y | Y |
| In Release Notes | Y | Y | Y | Y |
| Availability | By invitation or request | Y | Y | Y |
| Documentation | Limited | Y | Y | N/A |

### Tech Preview (TP)

Features in Tech Preview (TP) are managed and supported by the Product Team and have been internally validated for a set of use cases. Minimal documentation is directly provided to customers and is normally not publicly hosted.

TP features aren't supported by Customer Support and may change at any time during the preview.

If you are interested in a TP feature, contact {{% support-general %}}.

{{<tip title="Enabling preview features">}}
Configuration of preview feature-specific flags is restricted to those listed in the [allowed_preview_flags_csv](../../reference/configuration/yb-master/#allowed-preview-flags-csv). This measure acts as a safeguard, ensuring users are consciously aware they are modifying settings for preview features.
{{</tip>}}

Participating customers agree to provide feedback to the Product Team. However, the timeline for addressing specific areas of feedback (including bugs) is at the discretion of Yugabyte.

Documentation (if provided) for TP features is marked as such.

### Early Access (EA)

A feature in Early Access (EA) is new or enhanced functionality made available for you to use.

Code is well tested. Typically these features are not enabled by default. Enabling the feature is considered safe.

Support for the overall feature will not be dropped, though details may change in a subsequent GA release.

For production use cases, it is recommended to validate the use case with the Yugabyte Product team. Refer to product documentation for specific limitations.

Any bug fixes or improvements are managed and fixed with the same timeline and processes as those in GA.

EA features in YugabyteDB Aeon are not subject to the YugabyteDB Aeon SLA.

Give feedback on EA features on our [Slack community]({{<slack-invite>}}) or by filing a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues).

Documentation for EA features is marked as such.

### General Availability (GA)

A feature in General Availability (GA) is enabled and accessible by default for all customers.

GA features are supported by Yugabyte Support, and issues are addressed according to the [stable release support policy](../#stable-release-support-policy).

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
