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
| Red Hat Enterprise Linux&nbsp;9.3 | {{<icon/yes>}} |  | Supported in v2.20.3 and later.  {{<badge/ea>}} |
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

## CIS Hardened

YugabyteDB supports RHEL CIS hardened OSs based on the following images:

- [CIS Red Hat Enterprise Linux 8 Benchmark-Level 1](https://aws.amazon.com/marketplace/pp/prodview-kg7ijztdpvfaw?sr=0-7&?ref=_ptnr_cis_website)

- [CIS Red Hat Enterprise Linux 9 Benchmark-Level 1](https://aws.amazon.com/marketplace/server/procurement?productId=fa2dc596-6685-4c0b-b258-3c415342c908)

These images have been customized for YugabyteDB and YugabyteDB Anywhere, as described in the following sections.

### Port changes required for YugabyteDB

Firewall rules were changed to add the [default ports](default-ports/) required by YugabyteDB:

```sh
#!/bin/bash

sudo dnf repolist
sudo dnf config-manager --set-enabled extras
sudo dnf install -y firewalld
sudo systemctl start firewalld


ports=(5433 9042 7100 9100 18018 9070 7000 9000 15433)

for port in "${ports[@]}"; do
   sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done

sudo firewall-cmd --reload
```

### Firewall changes required for YugabyteDB Anywhere

Firewall rules were changed to allow [YBA Installer](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/) to install YugabyteDB Anywhere on CIS hardened Linux.

```sh
#!/bin/bash

sudo dnf repolist
sudo dnf config-manager --set-enabled extras
sudo dnf install -y firewalld
sudo systemctl start firewalld

ports=(9090 9300 443 80 22)

for port in "${ports[@]}"; do
   sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done

sudo firewall-cmd --reload
```

For more information on networking requirements for YuigabyteDB Anywhere, refer to [Networking](../../../yugabyte-platform/prepare/networking/)

### Custom /tmp directory for on-premises providers

If you use the default `/tmp` directory, prechecks fail when deploying universe with an access denied error.

To resolve this, do the following:

1. Create a custom `tmp` directory called `/new_tmp` on the database nodes:

    ```sh
    sudo mkdir -p /new_tmp; sudo chown yugabyte:yugabyte -R /new_tmp
    ```

1. Set a remote `tmp` directory runtime configuration:

    ```sh
    yb.filepaths.remoteTmpDirectory: /new_tmp
    ```

1. Set YB-Master and YB-TServer flags of the universe with the custom `tmp` dir:

    "Tmp_dir": /new_tmp

#### Add yugabyte user to sshd_config

When provisioning nodes, YugabyteDB Anywhere adds the `yugabyte` user. If you are using a CIS hardened image and want SSH access to database nodes, you need to manually add the `yugabyte` user to `sshd_config`.

```sh
add_cmd = (
           f"sudo sed -i '/^AllowUsers / s/$/ yugabyte/' /etc/ssh/sshd_config && "
           "sudo systemctl restart sshd"
       )
```
