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

Unless otherwise noted, x86 operating systems are supported on all supported versions of YugabyteDB and YugabyteDB Anywhere. ARM support was added in v2.18.1.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :--- |
| CentOS7          | {{<icon/yes>}} |                |
| CentOS8          | {{<icon/yes>}} |                |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |
| Ubuntu 18        | {{<icon/yes>}} |                |
| Ubuntu 20        | {{<icon/yes>}} |                |
| Ubuntu 22        | {{<icon/yes>}} |                | 2.18.5, 2.20.1 |
| Red Hat Enterprise Linux 7 | {{<icon/yes>}} |      |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| SUSE Linux Enterprise Server 15 SP4 | {{<icon/yes>}} |   | {{<badge/ea>}} |
| Amazon Linux 2   | {{<icon/yes>}} | {{<icon/yes>}} |

## YugabyteDB Anywhere

YugabyteDB Anywhere supports the following operating systems and architectures.

Unless otherwise noted, operating systems are supported on all supported versions of YugabyteDB and YugabyteDB Anywhere.

| Operating system | x86  | ARM  | Notes |
| :--------------- | :--- | :--- | :--- |
| CentOS7 | {{<icon/yes>}} |   | Default for YBA-deployed nodes |
| CentOS8 | {{<icon/yes>}} |   |
| AlmaLinux 8 | {{<icon/yes>}} | {{<icon/yes>}} |
| AlmaLinux 9 | {{<icon/yes>}} | {{<icon/yes>}} |
| Ubuntu 18 | {{<icon/yes>}} |   | |
| Ubuntu 20 | {{<icon/yes>}} |   | |
| Ubuntu 22 | {{<icon/yes>}} |   | 2.18.5, 2.20.1 |
| Red Hat Enterprise Linux 7 | {{<icon/yes>}} |   |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |   |
| SUSE Linux Enterprise Server 15 SP4 | {{<icon/yes>}} |   | {{<badge/tp>}} |
| Amazon Linux 2 | {{<icon/yes>}} | {{<icon/yes>}} | 2.18.0 and later |

YugabyteDB Anywhere may also work on other Linux distributions; contact your YugabyteDB support representative if you need added support.
