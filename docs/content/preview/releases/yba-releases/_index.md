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

## Releases

| YBA Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.21](v2.21/) <span class='metadata-tag-gray'>Preview</span> | {{< yb-eol-dates "v2.21" release >}} | n/a | n/a |
| [v2.20](v2.20/) <span class='metadata-tag-green'>LTS</span> | {{< yb-eol-dates "v2.20" release >}} | {{< yb-eol-dates "v2.20" EOM >}} | {{< yb-eol-dates "v2.20" EOL >}} |
| [v2.18](v2.18/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2.18" release >}} | {{< yb-eol-dates "v2.18" EOM >}} | {{< yb-eol-dates "v2.18" EOL >}} |
| [v2.16](v2.16/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2.16" release >}} | {{< yb-eol-dates "v2.16" EOM >}} | {{< yb-eol-dates "v2.16" EOL >}} |
| [v2.14](../ybdb-releases/v2.14/) <span class='metadata-tag-green'>LTS</span> | {{< yb-eol-dates "v2.14" release >}} | {{< yb-eol-dates "v2.14" EOM >}} | {{< yb-eol-dates "v2.14" EOL >}} |

For end-of-life releases, see [Releases at end of life](../ybdb-releases/#eol-releases).

For information on release support policy, see [Release support policy](../#release-support-policy).

For information on release versioning, see [Versioning](../versioning/).

## Compatibility with YugabyteDB

YugabyteDB Anywhere (YBA) is a control plane for deploying and managing YugabyteDB universes. You can use YBA to deploy universes with an equivalent or earlier version of YugabyteDB.

### Supported versions

Every version of YBA supports the same version and prior releases of YugabyteDB, down to and including the two preceding LTS release series and any intervening STS releases. This provides a span of support of approximately 2 years.

YBA v2.20.x supports the following YugabyteDB release series:

- [v2.20.x](../ybdb-releases/v2.20/) (LTS)
- [v2.18.x](../ybdb-releases/v2.18/) (STS)
- [v2.16.x](../ybdb-releases/v2.16/) (STS)
- [v2.14.x](../ybdb-releases/v2.14/) (LTS)

Qualification tests for each new version of YugabyteDB Anywhere are run on the latest version of YugabyteDB in each release series.

{{< warning title="YugabyteDB v2.14 and v2.18 End of Maintenance" >}}
v2.14 and v2.18 will reach end of maintenance in mid-2024. If you are running universes on these release series, you should consider upgrading those universes to the next LTS release series (v2.20).
{{< /warning >}}

For information on managing YugabyteDB releases and upgrading universes using YBA, refer to [Upgrade the YugabyteDB software](../../yugabyte-platform/manage-deployments/upgrade-software/).

For information on YugabyteDB release support timelines, refer to [YugabyteDB releases](../ybdb-releases).

### Upgrading YBA

Keep YBA up-to-date with the latest stable version to get the latest fixes and improvements, as well as to be able to deploy the latest releases of YugabyteDB.

Note that you cannot deploy or upgrade a universe to a higher version of YugabyteDB than the version of YugabyteDB Anywhere. To upgrade a universe to a more recent version of YugabyteDB, you may first have to upgrade YugabyteDB Anywhere.

- For YugabyteDB upgrades in YBA, you can only upgrade from a _stable_ version to another _stable_ version, or from a _preview_ version to another _preview_ version. Optionally, you can [skip tests](#skip-tests) during upgrades.

- For YBA upgrades, you can only upgrade from a _stable_ version to another _stable_ version, or from a _preview_ version to another _preview_ version. Optionally, you can [skip tests](#skip-tests) during upgrades.

{{< warning title="Replicated end of life" >}}
YugabyteDB Anywhere will end support for Replicated installation at the end of 2024. You can migrate existing Replicated YugabyteDB Anywhere installations using YBA Installer. To perform the migration, you must first upgrade to YBA v2.20.1 or later using Replicated.
{{< /warning >}}

For information on upgrading YugabyteDB Anywhere, refer to [Upgrade YugabyteDB Anywhere](../../yugabyte-platform/upgrade/).

#### Skip tests

Optionally, you can set a runtime flag `yb.skip_version_checks`, to skip all YugabyteDB and YBA version checks during upgrades. For more information, contact {{% support-platform %}}.
