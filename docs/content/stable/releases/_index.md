---
title: Releases in YugabyteDB
headerTitle: Releases
linkTitle: Releases
description: Release support policy, versioning, and feature availability for YugabyteDB.
aliases:
  - /stable/releases/releases-overview/
  - /stable/releases/whats-new/
type: indexpage
showRightNav: true
cascade:
  unversioned: true
---

This section describes the release support policy, versioning, and feature availability for YugabyteDB and YugabyteDB Anywhere. You can also access releases and release notes for YugabyteDB and YugabyteDB Anywhere.

-> For information on YugabyteDB Aeon releases, refer to the [YugabyteDB Aeon Change log](../yugabyte-cloud/release-notes/).

-> For information on YugabyteDB Voyager releases, refer to the [YugabyteDB Voyager release notes](../yugabyte-voyager/release-notes/).

## Current releases

<ul class="nav yb-pills">
  <li>
    <a href="ybdb-releases/">
        <img src="/icons/database.svg" alt="Server Icon"><span>YugabyteDB -></span>
    </a>
  </li>
  <li>
    <a href="yba-releases/">
        <img src="/icons/server.svg" alt="Server Icon"><span>YugabyteDB Anywhere -></span>
    </a>
  </li>
</ul>

YugabyteDB and YugabyteDB Anywhere have three kinds of releases:

- Stable, with Long-term support (LTS)
- Stable, with Standard-term support (STS)
- Preview, with no official support

Preview releases, which include features under active development are recommended for development and testing only.

For details about the differences between the release series, see [Release versioning](versioning/).

## Upcoming release series

The table below includes tentative release dates for upcoming release series, subject to change.

For information on key features planned for the upcoming releases, visit [Current roadmap](https://github.com/yugabyte/yugabyte-db#current-roadmap).

| Release series | Planned release |
| :------------- | :-------------- |
| v2026.1 (Next STS) | Mid 2026 |
| v2026.2 (Next LTS) | End 2026 |

## Recommended release series for projects

Yugabyte recommends that customers use a stable release (STS or LTS) for production.

In exceptional cases, if new feature(s) are critical for a project, and if a customer confirms with Yugabyte that such feature(s) are actually included in an upcoming stable release that aligns to the project's production timeline, then the customer can use a preview release.

For example, if your production go-live is more than three months from now, you may consider starting your development and testing with the current preview release series. By the time your production is ready to go live, the newly-released and most current stable release series (which is based on the preview release series you used for development and testing) should be available. By following this guideline, you can enable your application to be developed with the latest available features and enhancements, while maximizing quality and stability.
