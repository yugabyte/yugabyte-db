---
title: YugabyteDB Anywhere releases
headerTitle: YugabyteDB Anywhere releases
linkTitle: YugabyteDB Anywhere releases
description: An overview of YugabyteDB Anywhere releases, including preview and current stable releases.
image: /images/section_icons/index/quick_start.png
type: indexpage
showRightNav: true
cascade:
  unversioned: true
---

## Current supported releases

The supported release series include:

* [LTS and STS](../versioning/#stable-releases): Supported for production deployments.
* [Preview](../versioning/#preview-releases): Supported for development and testing only.

For details about the differences between the release series, see [Release versioning](../versioning/).

### Release support policy

The type of YugabyteDB release series you are running determines its support timelines.

**LTS (long-term support)** release series receive maintenance updates for at least 2 years (730 days) from the first release date of the minor release.

**STS (standard-term support)** release series receive maintenance updates for at least 1 year (365 days) from the first release date of the minor release.

LTS and STS release series are both subject to the following support and EOL timelines:

* **Extended support:** Following the maintenance support period, Yugabyte will provide support for at least an additional 180 days subject to the following guidelines:
  * Updates and Upgrades will not be made to the minor release.
  * Yugabyte will direct Customers to existing Updates and workarounds applicable to the reported case.
  * Yugabyte may direct Customers to Upgrade to a current release if a workaround does not exist.
* **End of Life (EOL):** Yugabyte will post publicly on its website a notice of End of Life (EOL) for the affected Software and the timeline for discontinuing Support Services.

The information in this section is a summary for convenience only. For complete details, see the [Yugabyte Support Services Agreement](https://www.yugabyte.com/yugabyte-software-support-services-agreement/).

### Release support timelines

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.19](v2.19/) <span class='metadata-tag-gray'>Preview</span> | {{< yb-eol-dates "v2.19" release >}} | n/a | n/a |
| [v2.18](v2.18/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2.18" release >}} | {{< yb-eol-dates "v2.18" EOM >}} | {{< yb-eol-dates "v2.18" EOL >}} |
| [v2.16](v2.16/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2.16" release >}} | {{< yb-eol-dates "v2.16" EOM >}} | {{< yb-eol-dates "v2.16" EOL >}} |
| [v2.14](../release-notes/v2.14/) <span class='metadata-tag-green'>LTS</span> | {{< yb-eol-dates "v2.14" release >}} | {{< yb-eol-dates "v2.14" EOM >}} | {{< yb-eol-dates "v2.14" EOL >}} |
| [v2.12](../release-notes/v2.12/) | {{< yb-eol-dates "v2.12" release >}} | {{< yb-eol-dates "v2.12" EOM >}} | {{< yb-eol-dates "v2.12" EOL >}} |
| [v2.8](../release-notes/v2.8/) | {{< yb-eol-dates "v2.8" release >}} | {{< yb-eol-dates "v2.8" EOM >}} | {{< yb-eol-dates "v2.8" EOL >}} |

### Releases at end of life (EOL) {#eol-releases}

The following releases are no longer supported:

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.17](v2.17/) | December 8, 2022 | n/a | n/a |
| [v2.15](../release-notes/v2.15/) | June 27, 2022 | n/a | n/a |
| [v2.13](../release-notes/v2.13/) | March 7, 2022 | n/a | n/a |
| [v2.11](../release-notes/v2.11/) | November 22, 2021 | n/a | n/a |
| [v2.9](../release-notes/v2.9/) | August 31, 2021 | n/a | n/a |
| [v2.7](../release-notes/v2.7/) | May 5, 2021 | n/a | n/a |
| [v2.6](../release-notes/v2.6/) | July 5, 2021 | July 5, 2022 | January 5, 2023 |
| [v2.5](../release-notes/v2.5/) | November 12, 2020 | n/a | n/a |
| [v2.4](../release-notes/v2.4/) | January 22, 2021 | January 22, 2022 | July 21, 2022 |
| [v2.2](../release-notes/v2.2/) | July 15, 2020 | July 15, 2021 | January 15, 2022 |
| [v2.1](../release-notes/v2.1/) | February 25, 2020 | February 25, 2021 | August 08, 2021 |
| [v2.0](../release-notes/v2.0/) | September 17, 2019 | September 17, 2020 | March 03, 2021 |
| [v1.3](../release-notes/v1.3/) | July 15, 2019 | July 15, 2020 | January 15, 2021 |

All non-current preview releases are unsupported.

{{<note title="Archived docs available">}}
Documentation for EOL stable releases is available at the [YugabyteDB docs archive](https://docs-archive.yugabyte.com/).
{{</note>}}

## Upcoming release series

The table below includes tentative release dates for upcoming release series, subject to change.

For information on key features planned for the upcoming releases, visit [Current roadmap](https://github.com/yugabyte/yugabyte-db#current-roadmap).

| Release series | Planned release |
| :------------- | :-------------- |
| v2.20 (STS) | Late 2023 |

## Recommended release series for projects

To ensure that your production go-live uses the most up-to-date stable release, follow this guideline.

| Production go-live | Recommended release series |
| :----------------- | :------------------------- |
| Less than 3 months | [{{<yb-version version="stable" format="displayName">}}](../release-notes/{{<yb-version version="stable" format="series">}}/) |
| More than 3 months | [{{<yb-version version="preview" format="displayName">}}]({{<yb-version version="preview" format="series">}}/) |

If your production go-live is more than three months from now, start your development and testing with the preview release series. By the time your production is ready to go live, the current stable release series (which is based on the preview release series you used for development and testing) will be available. By following this guideline, you ensure that your application is developed with the latest available features, enhancements, and fixes.
