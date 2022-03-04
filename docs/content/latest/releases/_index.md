---
title: Releases overview
headerTitle: Releases overview
linkTitle: Releases overview
description: An overview of the latest and current stable releases, and upcoming releases.
image: /images/section_icons/index/quick_start.png
aliases:
  - /latest/releases/releases-overview/
  - /latest/releases/whats-new/
section: RELEASES
menu:
  latest:
    identifier: releases
    weight: 100
isTocNested: true
showAsideToc: true
---

{{< tip title="Versioning" >}}
As of v2.2, Yugabyte follows a new release versioning convention for YugabyteDB and Yugabyte Platform releases. For details, see [Release versioning](versioning/).
{{< /tip >}}

## On this page

* Current [supported releases](#current-supported-releases)
* Yugabyte's [release support policy](#release-support-policy) and [timelines](#release-support-timelines
* Releases that are [no longer supported](#eol-releases)
* [Release series recommendations](#recommended-release-series-for-projects) for your project

## Current supported releases

The supported release series include:

* [Stable](versioning/#stable-releases): Supported for production deployments.
* [Latest](versioning/#latest-releases): Supported for development and testing only.

For details about the differences between the stable and latest release series, see [Release versioning](versioning/).

### Release support policy

Support for YugabyteDB stable release series includes:

* **Maintenance support:** For at least 1 year from the date of the first minor release, Yugabyte will provide Updates for such release.
* **Extended support:** Following the maintenance support period, Yugabyte will provide support for at least an additional 180 days subject to the following guidelines:
  * Updates and Upgrades will not be made to the minor release.
  * Yugabyte will direct Customers to existing Updates and workarounds applicable to the reported case.
  * Yugabyte may direct Customers to Upgrade to a current release if a workaround does not exist.
* **End of Life (EOL):** Yugabyte will post publicly on its website a notice of End of Life (EOL) for the affected Software and the timeline for discontinuing Support Services.

For details, see the [Yugabyte Support Policy](https://www.yugabyte.com/support-policy/).

### Release support timelines

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.12](release-notes/v2.12/) ![CURRENT STABLE](/images/releases/current-stable.png) | February 22, 2022 | February 22, 2023 | August 22, 2023 |
| [v2.11](release-notes/v2.11/) ![LATEST](/images/releases/latest.png) | November 22, 2021 | n/a | n/a |
| [v2.8](release-notes/v2.8/) | November 15, 2021 | November 15, 2022 | June 15, 2023 |
| [v2.6](release-notes/v2.6/) | July 5, 2021 | July 5, 2022 | January 5, 2023 |
| [v2.4](release-notes/v2.4/) | January 22, 2021 | January 22, 2022 | July 21, 2022 |

### Releases at end of life (EOL) {#eol-releases}

The following releases are no longer supported:

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.9](release-notes/v2.9/) | August 31, 2021 | n/a | n/a |
| [v2.7](release-notes/v2.7/) | May 5, 2021 | n/a | n/a |
| [v2.5](release-notes/v2.5/) | November 12, 2020 | n/a | n/a |
| [v2.3](release-notes/v2.3/) | September 08, 2020 | n/a | n/a |
| [v2.2](release-notes/v2.2/) | July 15, 2020 | July 15, 2021 | January 15, 2022 |
| [v2.1](release-notes/v2.1/) | February 25, 2020 | February 25, 2021 | August 08, 2021 |
| [v2.0](release-notes/v2.0/) | September 17, 2019 | September 17, 2020 | March 03, 2021 |
| [v1.3](release-notes/v1.3/) | July 15, 2019 | July 15, 2020 | January 15, 2021 |

## Upcoming release series

The table below includes tentative release dates for upcoming release series, subject to change.

For information on key features planned for the upcoming releases, visit [Current roadmap](https://github.com/yugabyte/yugabyte-db#current-roadmap).

| Release series | Planned release |
| :------------- | :-------------- |
| v2.14 (stable) | Mid-2022 |
| v2.13 | Early 2022 |

## Recommended release series for projects

To ensure that your production go-live uses the most up-to-date stable release, follow this guideline.

| Production go-live | Recommended release series |
| :----------------- | :------------------------- |
| Less than 3 months | v2.12 (current stable)     |
| More than 3 months | v2.13 (latest)             |

If your production go-live is more than three months from now, start your development and testing with the latest release series. By the time your production is ready to go live, the current stable release series (which is based on the latest release series you used for development and testing) will be available. By following this guideline, you ensure that your application is developed with the latest available features, enhancements, and fixes.
