---
title: Releases overview
headerTitle: Releases overview
linkTitle: Releases overview
description: An overview of the latest and current stable releases, and upcoming releases.
section: RELEASES
menu:
  latest:
    identifier: releases-overview
    weight: 2585
isTocNested: true
showAsideToc: true
---

{{< note title="Important" >}}

As of v2.2, Yugabyte follows a new release versioning convention for YugabyteDB and Yugabyte Platform releases.  For details, see [Release versioning](../versioning).

{{< /note >}}

## Current supported releases

The following release series include:

- [Stable](../versioning/#stable-releases): Supported for production deployments.
- [Latest](../versioning/#latest-releases): Supported for development and testing only.

For details about the differences between the stable and latest release series, see [Release versioning](../versioning).

### Release support policy

Support for YugabyteDB stable release series includes:

- **Maintenance support:** For at least 1 year from the minor release date, Yugabyte will provide Updates for such release.
- **Extended support:** Following the maintenance support period, Yugabyte will provide Updates for at least an additional 180 days subject to the following guidelines:
  - Updates and Upgrades will not be made to the minor release.
  - Yugabyte will direct Customers to existing Updates and workarounds applicable to the reported case.
  - Yugabyte may direct Customers to Upgrade to a current release if a workaround does not exist.
- **End of Life (EOL):** Yugabyte will post publicly on its website a notice of End of Life (EOL) for the affected Software and the timeline for discontinuing Support Services.

For details, see the [Yugabyte Support Policy](https://www.yugabyte.com/support-policy/).

### Release support timelines

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.11](../whats-new/latest-release) ![LATEST](/images/releases/latest.png) | November 22, 2021 | n/a | n/a |
| [v2.8](../whats-new/stable-release) ![CURRENT STABLE](/images/releases/current-stable.png) | November 15, 2021 | November 15, 2022 | June 15, 2023 |
| [v2.9](../earlier-releases/v2.9) | August 31, 2021 | n/a | n/a |
| [v2.7](../earlier-releases/v2.7) | May 5, 2021 | n/a | n/a |
| [v2.6](../earlier-releases/v2.6) | July 5, 2021 | July 5, 2022 | January 5, 2023 |
| [v2.5](../earlier-releases/v2.5) | November 12, 2020 | n/a | n/a |
| [v2.4](../earlier-releases/v2.4) | January 22, 2021 | January 22, 2022 | July 21, 2022 |
| [v2.3](../earlier-releases/v2.3.0) | September 08, 2020 | n/a | n/a |
| [v2.2](../earlier-releases/v2.2.0) | July 15, 2020 | July 15, 2021 | January 15, 2022 |
| [v2.1](../earlier-releases/v2.1.0) | February 25, 2020 | February 25, 2021 | August 08, 2021 |
| [v2.0](../earlier-releases/v2.0.0) | September 17, 2019 | September 17, 2020 | March 03, 2021 |
| [v1.3](../earlier-releases/v1.3.0) | July 15, 2019 | July 15, 2020 | January 15, 2021 |

**Note:** Supported stable release series include v2.1, v2.0, and v1.3, which were released prior to the new release versioning convention.

## Upcoming release series

The table below includes tentative release dates for upcoming release series, subject to change.

For information on key features planned for the upcoming releases, visit [Current roadmap](https://github.com/yugabyte/yugabyte-db#current-roadmap).

| Release series | Planned release |
| :------------- | :-------------- |
| v2.10 (stable) | Early 2022 |
| v2.13 | Early 2022 |

## Recommended release series for projects

To ensure that your production go-live uses the most up-to-date stable release, follow this guideline.

| Production go-live | Recommended release series |
| :----------------- | :------------------------- |
| < 3 months         | v2.8 (current stable)      |
| > 3 months         | v2.11 (latest)             |

If your production go-live is more than three months from now, start your development and testing with the latest release series. By the time your production is ready to go live, the current stable release series, based on the latest release series you used for development and testing, will be available. By following this guideline, you ensure that your application is developed with the latest available features, enhancements, and fixes.
