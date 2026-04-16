---
title: Software requirements for database nodes
headerTitle: Software requirements for database nodes
linkTitle: Software requirements
description: Software prerequisites for database nodes running YugabyteDB.
headContent: Operating system and additional software required for YugabyteDB
menu:
  v2.20_yugabyte-platform:
    identifier: server-nodes-software
    parent: server-nodes
    weight: 20
type: indexpage
showRightNav: true
---

The Linux OS and other software components on each database cluster node must meet the following minimum software requirements.

Depending on the [provider type](../../yba-overview/#provider-configurations) and permissions you grant, you may have to install all of these requirements manually, or YugabyteDB Anywhere will install it all automatically.

{{< warning title="Using disk encryption software with YugabyteDB" >}}
If you are using third party disk encryption software, such as Vormetric or CipherTrust, the disk encryption service must be up and running on the node before starting any YugabyteDB services. If YugabyteDB processes start _before_ the encryption service, restarting an already encrypted node can result in data corruption.

To avoid problems, [pause the universe](../../manage-deployments/delete-universe/#pause-a-universe) _before_ enabling or disabling the disk encryption service on universe nodes.
{{< /warning >}}

### Linux OS

YugabyteDB Anywhere supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/).

AlmaLinux OS 8 disk images are used by default, but you can specify a custom disk image and OS.

On Red Hat Enterprise Linux 8-based systems (Red Hat Enterprise Linux 8, Oracle Enterprise Linux 8.x, Amazon Linux 2), additionally, add the following line to `/etc/systemd/system.conf` and `/etc/systemd/user.conf`:

```sh
DefaultLimitNOFILE=1048576
```

_You must reboot the system for these two settings to take effect._

#### Transparent hugepages

Note: Only perform this step for [legacy provisioning](./software-on-prem-legacy/). This step is performed automatically during [automatic provisioning](./software-on-prem/).

Transparent hugepages (THP) should be enabled for optimal performance. Download and run the following script as root:

- [install-yb-enable-transparent-huge-pages-service.sh](/files/install-yb-enable-transparent-huge-pages-service.sh)

_You must reboot the system for these settings to take effect._

<details>
  <summary>More information</summary>

The script performs the following steps:

1. Create a one-shot systemd service for configuring THP settings.

    ```sh
    unit_filename="yb-enable-transparent-huge-pages.service"
    unit_filepath="/etc/systemd/system/"
    unit_file_full_path=${unit_filepath}${unit_filename}

    unit_file_definition=$(cat <<EOF
    [Unit]
    Description=YugabyteDB Enable Transparent Hugepages (THP)
    DefaultDependencies=no
    After=local-fs.target
    Before=sysinit.target

    [Service]
    Type=oneshot
    RemainAfterExit=yes
    ExecStart=/bin/sh -c '\
        echo always > /sys/kernel/mm/transparent_hugepage/enabled && \
        echo defer+madvise > /sys/kernel/mm/transparent_hugepage/defrag && \
        echo 0 > /sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_none'

    [Install]
    WantedBy=basic.target
    EOF
    )

    # Always perform this, because if we update settings, we always apply.
    echo "Configuring ${unit_file_full_path}"
    echo "${unit_file_definition}" > ${unit_file_full_path}
    ```

    This creates a one-shot systemd unit file under `/etc/systemd/system/yb-enable-transparent-huge-pages.service`.

1. Load all the services on the system and check the status of the newly created service.

    ```sh
    # Load the services
    echo "Loading and enabling service"

    systemctl daemon-reload
    systemctl enable ${unit_filename}
    systemctl start ${unit_filename}
    systemctl --no-pager status ${unit_filename}

    status=$(systemctl show yb-enable-transparent-huge-pages.service \
        --property=ExecMainStatus,ActiveState)

    exec_main_status=$(echo "$status" | grep ExecMainStatus | cut -d= -f2)
    active_state=$(echo "$status" | grep ActiveState | cut -d= -f2)

    if [[ "$exec_main_status" -ne 0 || "$active_state" != "active" ]]; then
      echo "Service failed: ExecMainStatus=$exec_main_status, ActiveState=$active_state"
      echo "Check status/logs for ${unit_file_full_path}"
    fi
    ```

1. Ensure that all the THP settings are correctly set.

    ```sh
    cat /sys/kernel/mm/transparent_hugepage/enabled 
    ```

    Should return "always".

    ```sh
    cat /sys/kernel/mm/transparent_hugepage/defrag 
    ```

    Should return "defer+madvise".

    ```sh
    cat /sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_none 0
    ```

    Should return 0.

</details>

### Additional software

YugabyteDB Anywhere requires the following additional software to be pre-installed on nodes:

- OpenSSH Server. Allowing SSH is optional. Using SSH is required in some [legacy on-premises deployment](../server-nodes-software/software-on-prem-legacy/) approaches. [Tectia SSH](../../create-deployments/connect-to-universe/#enable-tectia-ssh) is also supported.
- tar
- unzip
- policycoreutils-python-utils

#### Python for database nodes

Install Python 3.8 on the database nodes. (If you are using [Legacy on-premises provisioning](software-on-prem-legacy/), Python 3.5-3.8 is supported, and 3.6 is recommended.)

Install the Python SELinux package corresponding to your version of Python. You can use pip to do this. Ensure the version of pip matches the version of Python.

For example, you can install Python as follows:

```sh
sudo yum install python36
sudo pip3.8 install selinux
sudo ln -s /usr/bin/python3.6 /usr/bin/python
sudo rm /usr/bin/python3
sudo ln -s /usr/bin/python3.6 /usr/bin/python3
python3 -c "import selinux; import sys; print(sys.version)"
```

```output
> 3.6.19 (main, Sep 11 2024, 00:00:00)
> [GCC 11.5.0 20240719 (Red Hat 11.5.0-2)]
```

Alternately, if you are using the default version of python3, you might be able to install the python3-libselinux package.

#### CA certificates

By default, YugabyteDB Anywhere can automatically generate and copy self-signed TLS certificates used for node-to-node encryption in transit to universe nodes when the universe is created.

However, if you want to use your own CA certificates, you must manually copy them to universe nodes. (CA certificates can only be used with on-premises universes.)

In your certificate authority UI (for example, Venafi), generate the following:

- Server certificates to use for node-to-node encryption; that is, for the VMs to be used for universes.

    These certificates must be copied to each of the VMs you will use in your universes.

- A certificate to use for client-to-node encryption; that is, for encrypting traffic between the database cluster and applications and clients.

    This certificate must also be copied to your application client.

In addition, you add the certificates to YugabyteDB Anywhere.

For more information, refer to [CA certificates](../../security/enable-encryption-in-transit/#use-custom-ca-signed-certificates-to-enable-tls).

### Additional software for airgapped deployment

Additionally, if not connected to the public Internet (that is, airgapped); and not connected to a local Yum repository that contains the [additional software](#additional-software), database cluster nodes must also have the following additional software pre-installed:

- libcgroup and libcgroup-tools (for planned future use of the cgconfig service, for cgroups; can be omitted for YBA versions earlier than v2024.1)
- rsync, openssl (all linux)
- semanage-utils (for Debian-based systems)
- glibc-locale-source, glibc-langpack-en
- libatomic (for Redhat-based aarch64)
- libatomic1, libncurses6 (for Debian-based aarch64)
- chrony (for time synchronization). When using a Public Cloud Provider, chrony is the only choice. When using an On-Premises provider, chrony is recommended; ntpd and systemd-timesyncd are also supported.
