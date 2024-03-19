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

The following operating systems and architectures are supported for [deploying YugabyteDB](../../../deploy/manual-deployment/).

Unless otherwise noted, operating systems are supported by all supported versions of YugabyteDB and YugabyteDB Anywhere. YugabyteDB Anywhere added support for deploying YugabyteDB to ARM-based systems for non-Kubernetes platforms in v2.18.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform<br>Default for YBA-deployed nodes |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Amazon Linux 2   | {{<icon/yes>}} | {{<icon/yes>}} | v2.18.0 and later<br>Deprecated in v2.20.0; not supported in v2.21.0.0 release and [upcoming release series](../../../releases/#upcoming-release-series) |
| CentOS7          | {{<icon/yes>}} |                | Deprecated in v2.20.0; not supported in v2.21.0.0 release and [upcoming release series](../../../releases/#upcoming-release-series) |
| Oracle Linux 7         | {{<icon/yes>}} |                | Deprecated in v2.20.0; not supported in v2.21.0.0 release and [upcoming release series](../../../releases/#upcoming-release-series)|
| Oracle Linux 8         | {{<icon/yes>}} |                | |
| Red Hat Enterprise Linux 7 | {{<icon/yes>}} |      | Deprecated in v2.20.0; not supported in v2.21.0.0 release and [upcoming release series](../../../releases/#upcoming-release-series) |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| SUSE Linux Enterprise Server 15 SP5 | {{<icon/yes>}} |     | {{<badge/ea>}} |
| Ubuntu 18        | {{<icon/yes>}} | {{<icon/yes>}} | Deprecated in v2.20.0; not supported in v2.21.0.0 release and [upcoming release series](../../../releases/#upcoming-release-series) |
| Ubuntu 20        | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Ubuntu 22        | {{<icon/yes>}} | {{<icon/yes>}} | Supported in v2.18.5, v2.20.1 |
