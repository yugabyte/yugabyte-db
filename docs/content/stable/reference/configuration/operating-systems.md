---
title: Operating system support
headerTitle: Operating system support
linkTitle: Operating systems
description: Operating systems supported by YugabyteDB and YugabyteDB Anywhere.
menu:
  stable:
    identifier: operating-systems
    parent: configuration
    weight: 3000
type: docs
---

## YugabyteDB

The following table describes the operating system and architecture support for [deploying YugabyteDB](../../../deploy/manual-deployment/).

Unless otherwise noted, operating systems are supported by all supported versions of YugabyteDB and YugabyteDB Anywhere. YugabyteDB Anywhere added support for deploying YugabyteDB to ARM-based systems for non-Kubernetes platforms in v2.18.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform<br>Default for YBA-deployed nodes |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Oracle Linux 8   | {{<icon/yes>}} |                | |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| Red Hat Enterprise Linux&nbsp;9.3 | {{<icon/yes>}} |  | Supported in v2.20.3 and later.  {{<badge/ea>}} |
| SUSE&nbsp;Linux&nbsp;Enterprise&nbsp;Server&nbsp;15&nbsp;SP5 | {{<icon/yes>}} |     | {{<badge/ea>}} |
| Ubuntu 20        | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Ubuntu 22        | {{<icon/yes>}} | {{<icon/yes>}} | Supported in v2.18.5, v2.20.1 |

The following table describes operating systems and architectures that are no longer supported for deploying YugabyteDB.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| Amazon Linux 2   | {{<icon/no>}}  | {{<icon/no>}}  | Supported in v2.18.0 and later<br>Deprecated in v2.20 <br> Removed support in v2024.1. |
| CentOS 7          | {{<icon/no>}}  |                | Deprecated in v2.20<br> Removed support in v2024.1. |
| Oracle Linux 7   | {{<icon/no>}}  |                | Deprecated in v2.20<br> Removed support in v2024.1. |
| Red Hat Enterprise Linux 7 | {{<icon/no>}} |       | Deprecated in v2.20<br> Removed support in v2024.1. |
| Ubuntu 18        | {{<icon/no>}}  | {{<icon/no>}}  | Deprecated in v2.20<br> Removed support in v2024.1. |
