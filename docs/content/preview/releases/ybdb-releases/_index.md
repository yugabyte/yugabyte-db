---
title: YugabyteDB releases
headerTitle: YugabyteDB releases
linkTitle: Releases
description: An overview of YugabyteDB releases, including preview and current stable releases.
image: /images/section_icons/index/quick_start.png
type: indexpage
aliases:
  - /preview/releases/release-notes/
showRightNav: true
cascade:
  unversioned: true
---

## Releases

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2024.1](v2024.1/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2024.1" release >}} | {{< yb-eol-dates "v2024.1" EOM >}} | {{< yb-eol-dates "v2024.1" EOL >}} |
| [v2.21](v2.21/) <span class='metadata-tag-gray'>Preview</span> | {{< yb-eol-dates "v2.21" release >}} | No support | n/a |
| [v2.20](v2.20/) <span class='metadata-tag-green'>LTS</span> | {{< yb-eol-dates "v2.20" release >}} | {{< yb-eol-dates "v2.20" EOM >}} | {{< yb-eol-dates "v2.20" EOL >}} |
| [v2.18](v2.18/) <span class='metadata-tag-green'>STS</span> | {{< yb-eol-dates "v2.18" release >}} | {{< yb-eol-dates "v2.18" EOM >}} | {{< yb-eol-dates "v2.18" EOL >}} |
| [v2.14](v2.14/) <span class='metadata-tag-green'>LTS</span> | {{< yb-eol-dates "v2.14" release >}} | {{< yb-eol-dates "v2.14" EOM >}} | {{< yb-eol-dates "v2.14" EOL >}} |

{{< tip title="YugabyteDB Anywhere release notes have moved" >}}
Starting with v2.16, the [release notes for YugabyteDB Anywhere](../yba-releases/) have moved to their own page.
{{< /tip >}}

For information on stable release support policy, see [Stable Release support policy](../#stable-release-support-policy).

For information on release versioning, see [Versioning](../versioning/).

## Downloads

### v2024.1

{{<nav/tabs list="custom">}}
{{<nav/tab text="macOS" name="macos" active="true">}}
{{<nav/tab text="Linux x86" name="linux86">}}
{{<nav/tab text="Linux ARM" name="linuxarm">}}
{{<nav/tab text="Docker" name="docker">}}
{{</nav/tabs>}}

<!-- Panels begin-->
{{<nav/panels>}}

{{<nav/panel name="macos" active="true">}}

| Version | Binary |
|----| ------|
| {{<release "2024.1.2.0">}} | {{<download/link version="2024.1.2.0" build="b77" os="macos">}} |

{{</nav/panel>}}

{{<nav/panel name="linux86">}}

| Version | Binary |
|----| ------|
| {{<release "2024.1.2.0">}} | {{<download/link version="2024.1.2.0" build="b77" os="linux86">}} |

{{</nav/panel>}}

{{<nav/panel name="linuxarm">}}

| Version | Binary |
|----| ------|
| {{<release "2024.1.2.0">}} | {{<download/link version="2024.1.2.0" build="b77" os="linuxarm">}} |

{{</nav/panel>}}

{{<nav/panel name="docker">}}

| Version | Binary |
|----| ------|
| {{<release "2024.1.2.0">}} | `docker pull yugabytedb/yugabyte:2024.1.2.0-b77` |

{{</nav/panel>}}

{{</nav/panels>}}
<!-- Panels end-->

### v2.21

{{<nav/tabs list="none" repeatedTabs="true">}}
{{<nav/tab text="macOS" name="macos" active="true">}}
{{<nav/tab text="Linux x86" name="linux86">}}
{{<nav/tab text="Linux ARM" name="linuxarm">}}
{{<nav/tab text="Docker" name="docker">}}
{{</nav/tabs>}}

<!-- Panels begin-->
{{<nav/panels>}}

{{<nav/panel name="macos" active="true">}}

| Version | Binary |
|----| ------|
| {{<release "2.21.1.0">}} | {{<download/link version="2.21.1.0" build="b271" os="macos">}} |

#### Instructions

```bash
curl -O {{<download/link version="2.21.1.0" build="b271" os="macos" mode="plain">}}
tar xvfz {{<download/link version="2.21.1.0" build="b271" os="macos" mode="filename">}} && cd {{<download/link version="2.21.1.0" build="b271" os="macos" mode="dir">}}/
./bin/yugabyted start
```

{{</nav/panel>}}

{{<nav/panel name="linux86">}}

| Version | Binary |
|----| ------|
| {{<release "2.21.1.0">}} | {{<download/link version="2.21.1.0" build="b271" os="linux86">}} |

#### Instructions

```bash
wget {{<download/link version="2.21.1.0" build="b271" os="linux86" mode="plain">}}
tar xvfz {{<download/link version="2.21.1.0" build="b271" os="linux86" mode="filename">}} && cd {{<download/link version="2.21.1.0" build="b271" os="linux86" mode="dir">}}/
./bin/post_install.sh
./bin/yugabyted start
```

{{</nav/panel>}}

{{<nav/panel name="linuxarm">}}

| Version | Binary |
|----| ------|
| {{<release "2.21.1.0">}} | {{<download/link version="2.21.1.0" build="b271" os="linuxarm">}} |

#### Instructions

```bash
wget {{<download/link version="2.21.1.0" build="b271" os="linuxarm" mode="plain">}}
tar xvfz {{<download/link version="2.21.1.0" build="b271" os="linuxarm" mode="filename">}} && cd {{<download/link version="2.21.1.0" build="b271" os="linuxarm" mode="dir">}}/
./bin/post_install.sh
./bin/yugabyted start
```

{{</nav/panel>}}

{{<nav/panel name="docker">}}

| Version | Binary |
|----| ------|
| {{<release "2.21.1.0">}} | `docker pull yugabytedb/yugabyte:2.21.1.0-b271` |

#### Instructions

```bash
docker pull yugabytedb/yugabyte:2.21.1.0-b271
docker run -d --name yugabyte -p7000:7000 -p9000:9000 -p15433:15433 -p5433:5433 -p9042:9042  yugabytedb/yugabyte:2.21.1.0-b271 bin/yugabyted start  --background=false
```

{{</nav/panel>}}

{{</nav/panels>}}
<!-- Panels end-->

## Releases at end of life (EOL) {#eol-releases}

{{<note title="Archived docs available">}}
Documentation for EOL stable releases is available at the [YugabyteDB docs archive](https://docs-archive.yugabyte.com/).
{{</note>}}

The following stable and preview releases are no longer supported:

| Release series | Released | End of maintenance support | End of Life (EOL) |
| :------------- | :------- | :------------------------- | :---------------- |
| [v2.19](v2.19/) | March 8, 2024 | n/a | n/a |
| [v2.17](v2.17/) | December 8, 2022 | n/a | n/a |
| [v2.16](end-of-life/v2.16/) | December 14, 2022 | December 14, 2023 | June 14, 2024 |
| [v2.15](v2.15/) | June 27, 2022 | n/a | n/a |
| [v2.13](end-of-life/v2.13/) | March 7, 2022 | n/a | n/a |
| [v2.12](end-of-life/v2.12/) | February 22, 2022 | February 22, 2023 | August 22, 2023 |
| [v2.11](end-of-life/v2.11/) | November 22, 2021 | n/a | n/a |
| [v2.9](end-of-life/v2.9/) | August 31, 2021 | n/a | n/a |
| [v2.8](end-of-life/v2.8/) | November 15, 2021 | November 15, 2022 | May 15, 2023 |
| [v2.7](end-of-life/v2.7/) | May 5, 2021 | n/a | n/a |
| [v2.6](end-of-life/v2.6/) | July 5, 2021 | July 5, 2022 | January 5, 2023 |
| [v2.5](end-of-life/v2.5/) | November 12, 2020 | n/a | n/a |
| [v2.4](end-of-life/v2.4/) | January 22, 2021 | January 22, 2022 | July 21, 2022 |
| [v2.2](end-of-life/v2.2/) | July 15, 2020 | July 15, 2021 | January 15, 2022 |
| [v2.1](end-of-life/v2.1/) | February 25, 2020 | February 25, 2021 | August 08, 2021 |
| [v2.0](end-of-life/v2.0/) | September 17, 2019 | September 17, 2020 | March 03, 2021 |
| [v1.3](end-of-life/v1.3/) | July 15, 2019 | July 15, 2020 | January 15, 2021 |
