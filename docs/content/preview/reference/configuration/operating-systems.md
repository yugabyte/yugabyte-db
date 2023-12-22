---
title: Operating system support
headerTitle: Operating system support
linkTitle: Operating systems
description: Operating systems supported by YugabyteDB and YugabyteDB Anywhere.
menu:
  preview:
    identifier: operating-systems
    parent: configuration
    weight: 3000
type: docs
---

## YugabyteDB

YugabyteDB supports the following operating systems and architectures.

Unless otherwise noted, operating systems are supported on all supported versions of YugabyteDB and YugabyteDB Anywhere.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :--- |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |
| Amazon Linux 2   | {{<icon/yes>}} | {{<icon/yes>}} | Deprecated in v2.20.0 |
| CentOS7          | {{<icon/yes>}} |                | Deprecated in v2.20.0 |
| CentOS8          | {{<icon/yes>}} |                |
| Red Hat Enterprise Linux 7 | {{<icon/yes>}} |      |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| SUSE Linux Enterprise Server 15 SP4 | {{<icon/yes>}} |   | {{<badge/ea>}} |
| Ubuntu 18        | {{<icon/yes>}} |                | Deprecated in v2.20.0 |
| Ubuntu 20        | {{<icon/yes>}} |                |
| Ubuntu 22        | {{<icon/yes>}} |                | Supported in v2.18.5, v2.20.1 |

## YugabyteDB Anywhere

YugabyteDB Anywhere supports the following operating systems and architectures.

Unless otherwise noted, x86 operating systems are supported on all supported versions of YugabyteDB and YugabyteDB Anywhere. YugabyteDB Anywhere added ARM support in v2.18.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :--- |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} | Default for YBA-deployed nodes |
| Amazon Linux 2   | {{<icon/yes>}} | {{<icon/yes>}} | v2.18.0 and later |
| CentOS7          | {{<icon/yes>}} |                | |
| Oracle 7         | {{<icon/yes>}} |   |
| Oracle 8         | {{<icon/yes>}} |   |
| SUSE Linux Enterprise Server 15 SP4 | {{<icon/yes>}} |   | {{<badge/tp>}} |
| SUSE Linux Enterprise Server 15 SP5 | {{<icon/yes>}} |   | {{<badge/tp>}} |
| Ubuntu 18        | {{<icon/yes>}} |                | |
| Ubuntu 20        | {{<icon/yes>}} | {{<icon/yes>}} | |
| Ubuntu 22        | {{<icon/yes>}} |                | Supported in v2.18.5, v2.20.1 |

YugabyteDB Anywhere may also work on other Linux distributions; contact your YugabyteDB support representative if you need added support.
