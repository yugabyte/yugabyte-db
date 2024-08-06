---
title: Operating system support
headerTitle: Operating system support
linkTitle: Operating systems
description: Operating systems supported by YugabyteDB and YugabyteDB Anywhere.
menu:
  v2.20:
    identifier: operating-systems
    parent: configuration
    weight: 3000
type: docs
---

## YugabyteDB

The following operating systems and architectures are supported for [deploying YugabyteDB](../../../deploy/manual-deployment/).

Unless otherwise noted, operating systems are supported by all supported versions of YugabyteDB and YugabyteDB Anywhere. YugabyteDB Anywhere added support for deploying YugabyteDB to ARM-based systems for non-Kubernetes platforms in v2.18.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform<br>Default for YBA-deployed nodes |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Amazon Linux 2   | {{<icon/partial>}} | {{<icon/partial>}} |Supported in v2.18.0 and later<br>Deprecated in v2.20; not supported in v2.21 and subsequent release series. |
| Amazon Linux 2<br>(ARM, CIS-hardened) |  | {{<icon/partial>}} | Supported only in v2.20.x. Database performance in this environment varies both due to CIS-hardening and ARM. For more information, contact {{% support-general %}}.|
| CentOS 7          | {{<icon/partial>}} |                | Deprecated in v2.20; not supported in v2.20.6, v2.21 and subsequent release series. |
| Oracle 7         | {{<icon/partial>}} |                | Deprecated in v2.20; not supported in v2.20.6, v2.21 and subsequent release series. |
| Oracle 8         | {{<icon/yes>}} |                | |
| Red Hat Enterprise Linux 7 | {{<icon/partial>}} |      | Deprecated in v2.20; not supported in v2.20.6, v2.21 and subsequent release series.|
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| Red Hat Enterprise Linux&nbsp;9.3 | {{<icon/yes>}} |  | Supported in v2.20.3 and later.  {{<badge/ea>}} |
| SUSE&nbsp;Linux&nbsp;Enterprise&nbsp;Server&nbsp;15&nbsp;SP5 | {{<icon/yes>}} |     | {{<badge/ea>}} |
| Ubuntu 18        | {{<icon/partial>}} | {{<icon/partial>}} | Deprecated in v2.20; not supported in v2.21 and subsequent release series. |
| Ubuntu 20        | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Ubuntu 22        | {{<icon/yes>}} | {{<icon/yes>}} | Supported in v2.18.5, v2.20.1 |
