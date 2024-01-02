---
title: Releases in YugabyteDB
headerTitle: Releases
linkTitle: Releases
description: Release support policy, versioning, and feature availability for YugabyteDB.
image: /images/section_icons/index/quick_start.png
aliases:
  - /preview/releases/releases-overview/
  - /preview/releases/whats-new/
type: indexpage
showRightNav: true
cascade:
  unversioned: true
---

This section describes the release support policy, versioning, and feature availability for YugabyteDB and YugabyteDB Anywhere. You can also access releases and release notes for YugabyteDB and YugabyteDB Anywhere.

-> For information on YugabyteDB Managed releases, refer to the [YugabyteDB Managed Change log](../yugabyte-cloud/release-notes/).

-> For information on YugabyteDB Voyager releases, refer to the [YugabyteDB Voyager release notes](../yugabyte-voyager/release-notes/).

## Current releases

<ul class="nav yb-pills">
  <li>
    <a href="release-notes/">
        <img src="/icons/database.svg" alt="Server Icon"><span>YugabyteDB -></span>
    </a>
  </li>
  <li>
    <a href="yba-releases/">
        <img src="/icons/server.svg" alt="Server Icon"><span>YugabyteDB Anywhere -></span>
    </a>
  </li>
</ul>

The supported release series for YugabyteDB and YugabyteDB Anywhere include Long-term support (LTS) and standard-term support (STS).

Preview releases, which include features under active development, are also available. Preview releases are recommended for development and testing only.

For details about the differences between the release series, see [Release versioning](versioning/).

### Release support policy

The type of YugabyteDB release series you are running determines its support timelines.

**LTS** release series receive maintenance updates for at least 2 years (730 days) from the first release date of the minor release.

**STS** release series receive maintenance updates for at least 1 year (365 days) from the first release date of the minor release.

LTS and STS release series are both subject to the following support and EOL timelines:

* **Extended support:** Following the maintenance support period, Yugabyte will provide support for at least an additional 180 days subject to the following guidelines:
  * Updates and Upgrades will not be made to the minor release.
  * Yugabyte will direct Customers to existing Updates and workarounds applicable to the reported case.
  * Yugabyte may direct Customers to Upgrade to a current release if a workaround does not exist.
* **End of Life (EOL):** Yugabyte will post publicly on its website a notice of End of Life (EOL) for the affected Software and the timeline for discontinuing Support Services.

  {{<note title="Archived docs available">}}
Documentation for EOL stable releases is available at the [YugabyteDB docs archive](https://docs-archive.yugabyte.com/).
  {{</note>}}

The information in this section is a summary for convenience only. For complete details, see the [Yugabyte Support Services Agreement](https://www.yugabyte.com/yugabyte-software-support-services-agreement/).

## Upcoming release series

The table below includes tentative release dates for upcoming release series, subject to change.

For information on key features planned for the upcoming releases, visit [Current roadmap](https://github.com/yugabyte/yugabyte-db#current-roadmap).

| Release series | Planned release |
| :------------- | :-------------- |
| v2.22 (STS)    | Mid 2024       |

## Recommended release series for projects

To ensure that your production go-live uses the most up-to-date stable release, follow this guideline.

| Production go-live | Recommended release series |
| :----------------- | :------------------------- |
| Less than 3 months | [{{<yb-version version="stable" format="displayName">}}](release-notes/{{<yb-version version="stable" format="series">}}/) |
| More than 3 months | [{{<yb-version version="preview" format="displayName">}}](release-notes/{{<yb-version version="preview" format="series">}}/) |

If your production go-live is more than three months from now, start your development and testing with the preview release series. By the time your production is ready to go live, the current stable release series (which is based on the preview release series you used for development and testing) will be available. By following this guideline, you ensure that your application is developed with the latest available features, enhancements, and fixes.
