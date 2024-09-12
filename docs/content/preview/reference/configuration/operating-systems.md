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

The following table describes the operating system and architecture support for [deploying YugabyteDB](../../../deploy/manual-deployment/).

Unless otherwise noted, operating systems are supported by all supported versions of YugabyteDB and YugabyteDB Anywhere. YugabyteDB Anywhere added support for deploying YugabyteDB to ARM-based systems for non-Kubernetes platforms in v2.18.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| AlmaLinux 8      | {{<icon/yes>}} | {{<icon/yes>}} | Recommended for production<br>Recommended development platform<br>Default for YugabyteDB Anywhere-deployed nodes |
| AlmaLinux 9      | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Oracle Linux 8   | {{<icon/yes>}} |                | |
| Red Hat Enterprise Linux 8 | {{<icon/yes>}} |      | Recommended for production |
| Red Hat Enterprise Linux 8 CIS Hardened | {{<icon/yes>}} |      | |
| Red Hat Enterprise Linux&nbsp;9.3 and later | {{<icon/yes>}} |  | Supported in v2.20.3 and later.  {{<badge/ea>}} |
| Red Hat Enterprise Linux&nbsp;9 CIS Hardened | {{<icon/yes>}} |  | Supported in v2.20.3 and later.  {{<badge/ea>}} |
| SUSE&nbsp;Linux&nbsp;Enterprise&nbsp;Server&nbsp;15&nbsp;SP5 | {{<icon/yes>}} |     | {{<badge/ea>}} |
| Ubuntu 20        | {{<icon/yes>}} | {{<icon/yes>}} |       |
| Ubuntu 22        | {{<icon/yes>}} | {{<icon/yes>}} | Supported in v2.18.5, v2.20.1 |

The following table describes operating systems and architectures that are no longer supported for deploying YugabyteDB.

| Operating system | x86            | ARM            | Notes |
| :--------------- | :------------- | :------------- | :---- |
| Amazon Linux 2   | {{<icon/no>}}  | {{<icon/no>}}  | Supported in v2.18.0 and later<br>Deprecated in v2.20<br> Removed support in v2.21. |
| CentOS 7         | {{<icon/no>}}  |                | Deprecated in v2.20<br> Removed support in v2.21. |
| Oracle Linux 7   | {{<icon/no>}}  |                | Deprecated in v2.20<br> Removed support in v2.21. |
| Red Hat Enterprise Linux 7 | {{<icon/no>}} |       | Deprecated in v2.20<br> Removed support in v2.21. |
| Ubuntu 18        | {{<icon/no>}}  | {{<icon/no>}}  | Deprecated in v2.20<br> Removed support in v2.21. |

## Using CIS hardened operating systems

YugabyteDB supports RHEL CIS hardened operating systems based on the following images:

- [CIS Red Hat Enterprise Linux 8 Benchmark-Level 1](https://aws.amazon.com/marketplace/pp/prodview-kg7ijztdpvfaw?sr=0-7&?ref=_ptnr_cis_website)

- [CIS Red Hat Enterprise Linux 9 Benchmark-Level 1](https://aws.amazon.com/marketplace/server/procurement?productId=fa2dc596-6685-4c0b-b258-3c415342c908)

To use these images for YugabyteDB or YugabyteDB Anywhere, you need to make the following modifications.

### YugabyteDB clusters

To use a CIS hardened image for cluster nodes:

1. Install the image on the nodes.
1. [Change the firewall rules](../default-ports/#firewall-changes-for-cis-hardened-images).

### YugabyteDB Anywhere

To use a CIS hardened image for installing YugabyteDB Anywhere:

1. Install the image on the node.
1. [Change the firewall rules](../../../yugabyte-platform/prepare/networking/#firewall-changes-for-cis-hardened-images).

### YugabyteDB Anywhere universe

To use a CIS hardened image for on-premises universe nodes:

1. Install the image on the nodes.
1. [Change the firewall rules for YugabyteDB Anywhere](../../../yugabyte-platform/prepare/networking/#firewall-changes-for-cis-hardened-images).
1. Create a custom `tmp` directory during [provisioning](../../../yugabyte-platform/prepare/server-nodes-software/software-on-prem-manual/#custom-tmp-directory-for-cis-hardened-rhel-8-or-9).
1. After you create the [on-premises provider](../../../yugabyte-platform/configure-yugabyte-platform/on-premises-provider/), set the [provider runtime configuration](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) flag `yb.filepaths.remoteTmpDirectory` to the new tmp directory.
1. When creating universes using the provider, set YB-Master and YB-TServer [configuration flag](../../../yugabyte-platform/manage-deployments/edit-config-flags/) `tmp_dir` to the custom tmp directory.

To use a CIS hardened image for universes on cloud providers:

1. Download the image.
1. [Change the firewall rules for YugabyteDB Anywhere](../../../yugabyte-platform/prepare/networking/#firewall-changes-for-cis-hardened-images).
1. Upload the modified image to your cloud provider.
1. [Add the modified image](../../../yugabyte-platform/configure-yugabyte-platform/aws/) to the Linux version catalog.
