---
title: Manually provision on-premises nodes
headerTitle: Manually provision on-premises nodes
linkTitle: Manually provision on-prem nodes
description: Provision the on-premises nodes manually.
headContent: Your SSH user does not have sudo privileges
menu:
  v2.18_yugabyte-platform:
    identifier: on-premises-manual-2
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

Use the following procedure to manually provision nodes for your [on-premises provider configuration](../on-premises/):

- Your [SSH user](../on-premises/#ssh-key-pairs) has sudo privileges that require a password - **Manual setup with script**.
- Your SSH user does not have sudo privileges at all - **Fully manual setup**.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../on-premises-script/" class="nav-link">
      <i class="fa-regular fa-scroll"></i>
      Manual setup with script
    </a>
  </li>

  <li>
    <a href="../on-premises-manual/" class="nav-link active">
      <i class="icon-shell" aria-hidden="true"></i>
      Fully manual setup
    </a>
  </li>
</ul>

If the SSH user configured in the on-premises provider does not have sudo privileges, then you must set up each of the database nodes manually using the following procedure.

Note that you need access to a user with sudo privileges in order to complete these steps.

For each node, perform the following:

- [Set up time synchronization](#set-up-time-synchronization)
- [Open incoming TCP ports](#open-incoming-tcp-ip-ports)
- [Manually pre-provision the node](#pre-provision-nodes-manually)
- [Install Prometheus node exporter](#install-prometheus-node-exporter)
- [Install backup utilities](#install-backup-utilities)
- [Set crontab permissions](#set-crontab-permissions)
- [Install systemd-related database service unit files (optional)](#install-systemd-related-database-service-unit-files)
- [Install the node agent](#install-node-agent)

After you have provisioned the nodes, you can proceed to [add instances to the on-prem provider](../on-premises/#add-instances).

## Set up time synchronization

A local Network Time Protocol (NTP) server or equivalent must be available.

Ensure an NTP-compatible time service client is installed in the node OS (chrony is installed by default in the standard CentOS 7 instance used in this example). Then, configure the time service client to use the available time server. The procedure includes this step and assumes chrony is the installed client.

## Open incoming TCP/IP ports

Database servers need incoming TCP/IP access enabled to the following ports, for communications between themselves and YugabyteDB Anywhere:

| Protocol | Port | Description |
| :------- | :--- | :---------- |
| TCP | 22 | SSH (for automatic administration) |
| TCP | 5433 | YSQL client |
| TCP | 6379 | YEDIS client |
| TCP | 7000 | YB master webserver |
| TCP | 7100 | YB master RPC |
| TCP | 9000 | YB tablet server webserver |
| TCP | 9042 | YCQL client |
| TCP | 9090 | Prometheus server |
| TCP | 9100 | YB tablet server RPC |
| TCP | 9300 | Prometheus node exporter |
| TCP | 12000 | YCQL HTTP (for DB statistics gathering) |
| TCP | 13000 | YSQL HTTP (for DB statistics gathering) |
| TCP | 18018 | YB Controller |

The preceding table is based on the information on the [default ports page](../../../../reference/configuration/default-ports/).

## Pre-provision nodes manually

This process carries out all provisioning tasks on the database nodes which require elevated privileges. After the database nodes have been prepared in this way, the universe creation process from YugabyteDB Anywhere will connect with the nodes only via the `yugabyte` user, and not require any elevation of privileges to deploy and operate the YugabyteDB universe.

Physical nodes (or cloud instances) are installed with a standard CentOS 7 server image. The following steps are to be performed on each physical node, prior to universe creation:

1. Log in to each database node as a user with sudo enabled (the `centos` user in CentOS 7 images).

1. Add the following line to the `/etc/chrony.conf` file:

    ```text
    server <your-time-server-IP-address> prefer iburst
    ```

    Then run the following command:

    ```sh
    sudo chronyc makestep   # (force instant sync to NTP server)
    ```

1. Add a new `yugabyte:yugabyte` user and group with the default login shell `/bin/bash` that you set via the `-s` flag, as follows:

    ```bash
    sudo useradd -s /bin/bash --create-home --home-dir <yugabyte_home> yugabyte  # (add user yugabyte and create its home directory as specified in <yugabyte_home>)
    sudo passwd yugabyte   # (add a password to the yugabyte user)
    sudo su - yugabyte   # (change to yugabyte user for execution of next steps)
    ```

    `yugabyte_home` is the path to the Yugabyte home directory. If you set a custom path for the yugabyte user's home in the YugabyteDB Anywhere UI, you must use the same path here. Otherwise, you can omit the `--home-dir` flag.

    Ensure that the `yugabyte` user has permissions to SSH into the YugabyteDB nodes (as defined in `/etc/ssh/sshd_config`).

1. If the node is running SELinux and the home directory is not the default, set the correct SELinux ssh context, as follows:

    ```bash
    chcon -R -t ssh_home_t <yugabyte_home>
    ```

1. Copy the SSH public key to each DB node. This public key should correspond to the private key entered into the YugabyteDB Anywhere provider.

1. Run the following commands as the `yugabyte` user, after copying the SSH public key file to the user home directory:

    ```sh
    cd ~yugabyte
    mkdir .ssh
    chmod 700 .ssh
    cat <pubkey_file> >> .ssh/authorized_keys
    chmod 400 .ssh/authorized_keys
    exit   # (exit from the yugabyte user back to previous user)
    ```

1. Add the following lines to the `/etc/security/limits.conf` file (sudo is required):

    ```text
    *                -       core            unlimited
    *                -       data            unlimited
    *                -       fsize           unlimited
    *                -       sigpending      119934
    *                -       memlock         64
    *                -       rss             unlimited
    *                -       nofile          1048576
    *                -       msgqueue        819200
    *                -       stack           8192
    *                -       cpu             unlimited
    *                -       nproc           12000
    *                -       locks           unlimited
    ```

1. Modify the following line in the `/etc/security/limits.d/20-nproc.conf` file:

    ```text
    *          soft    nproc     12000
    ```

1. Install the rsync and OpenSSL packages (if not already included with your Linux distribution) using the following commands:

    ```sh
    sudo yum install openssl
    sudo yum install rsync
    ```

    For airgapped environments, make sure your Yum repository mirror contains these packages.

1. If running on a virtual machine, execute the following to tune kernel settings:

    1. Configure the parameter `vm.swappiness` as follows:

        ```sh
        sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
        sudo sysctl kernel.core_pattern=/home/yugabyte/cores/core_%p_%t_%E >> /etc/sysctl.conf
        ```

    1. Configure the parameter `vm.max_map_count` as follows:

        ```sh
        sudo sysctl -w vm.max_map_count=262144
        sudo bash -c 'sysctl vm.max_map_count=262144 >> /etc/sysctl.conf'
        ```

    1. Validate the change as follows:

        ```sh
        sysctl vm.max_map_count
        ```

1. Perform the following to prepare and mount the data volume (separate partition for database data):

    - List the available storage volumes, as follows:

      ```sh
      lsblk
      ```

    - Perform the following steps for each available volume (all listed volumes other than the root volume):

      ```sh
      sudo mkdir /data   # (or /data1, /data2 etc)
      sudo mkfs -t xfs /dev/nvme1n1   # (create xfs filesystem over entire volume)
      sudo vi /etc/fstab
      ```

    - Add the following line to `/etc/fstab`:

      ```text
      /dev/nvme1n1   /data   xfs   noatime   0   0
      ```

    - Exit from vi, and continue, as follows:

      ```sh
      sudo mount -av # (mounts the new volume using the fstab entry, to validate)
      sudo chown yugabyte:yugabyte /data
      sudo chmod 755 /data
      ```

## Install Prometheus node exporter

Download the 1.3.1 version of the Prometheus node exporter, as follows:

```sh
wget https://github.com/prometheus/node_exporter/releases/download/v1.3.1/node_exporter-1.3.1.linux-amd64.tar.gz
```

If you are doing an airgapped installation, download the node exporter using a computer connected to the internet and copy it over to the database nodes.

On each node, perform the following as a user with sudo access:

1. Copy the `node_exporter-1.3.1.linux-amd64.gz` package file that you downloaded into the `/tmp` directory on each of the YugabyteDB nodes. Ensure that this file is readable by the user (for example, `centos`).

1. Run the following commands:

    ```sh
    sudo mkdir /opt/prometheus
    sudo mkdir /etc/prometheus
    sudo mkdir /var/log/prometheus
    sudo mkdir /var/run/prometheus
    sudo mv /tmp/node_exporter-1.3.1.linux-amd64.tar.gz  /opt/prometheus
    sudo adduser --shell /bin/bash prometheus # (also adds group "prometheus")
    sudo chown -R prometheus:prometheus /opt/prometheus
    sudo chown -R prometheus:prometheus /etc/prometheus
    sudo chown -R prometheus:prometheus /var/log/prometheus
    sudo chown -R prometheus:prometheus /var/run/prometheus
    sudo chmod +r /opt/prometheus/node_exporter-1.3.1.linux-amd64.tar.gz
    sudo su - prometheus (user session is now as user "prometheus")
    ```

1. Run the following commands as user `prometheus`:

    ```sh
    cd /opt/prometheus
    tar zxf node_exporter-1.3.1.linux-amd64.tar.gz
    exit   # (exit from prometheus user back to previous user)
    ```

1. Edit the following file:

    ```sh
    sudo vi /etc/systemd/system/node_exporter.service
    ```

    Add the following to the `/etc/systemd/system/node_exporter.service` file:

    ```conf
    [Unit]
    Description=node_exporter - Exporter for machine metrics.
    Documentation=https://github.com/William-Yeh/ansible-prometheus
    After=network.target

    [Install]
    WantedBy=multi-user.target

    [Service]
    Type=simple

    User=prometheus
    Group=prometheus

    ExecStart=/opt/prometheus/node_exporter-1.3.1.linux-amd64/node_exporter  --web.listen-address=:9300 --collector.textfile.directory=/tmp/yugabyte/metrics
    ```

1. Exit from vi, and continue, as follows:

    ```sh
    sudo systemctl daemon-reload
    sudo systemctl enable node_exporter
    sudo systemctl start node_exporter
    ```

1. Check the status of the `node_exporter` service with the following command:

    ```sh
    sudo systemctl status node_exporter
    ```

## Install backup utilities

YugabyteDB Anywhere supports backing up YugabyteDB to Amazon S3, Azure Storage, Google Cloud Storage, and Network File System (NFS). For more information, see [Configure backup storage](../../../back-up-restore-universes/configure-backup-storage/).

You can install the backup utility for the backup storage you plan to use, as follows:

- NFS: Install rsync, which YugabyteDB Anywhere uses to perform NFS backups installed during one of the previous steps.

- Amazon S3: Install s3cmd, on which YugabyteDB Anywhere relies to support copying backups to Amazon S3. You have the following installation options:

  - For a regular installation, execute the following:

      ```sh
      sudo yum install s3cmd
      ```

  - For an airgapped installation, copy `/opt/third-party/s3cmd-2.0.1.tar.gz` from the YugabyteDB Anywhere node to the database node, and then extract it into the `/usr/local` directory on the database node, as follows:

      ```sh
      cd /usr/local
      sudo tar xvfz path-to-s3cmd-2.0.1.tar.gz
      sudo ln -s /usr/local/s3cmd-2.0.1/s3cmd /usr/local/bin/s3cmd
      ```

- Azure Storage: Install azcopy using one of the following options:

  - Download `azcopy_linux_amd64_10.13.0.tar.gz` using the following command:

      ```sh
      wget https://azcopyvnext.azureedge.net/release20211027/azcopy_linux_amd64_10.13.0.tar.gz
      ```

  - For airgapped installations, copy `/opt/third-party/azcopy_linux_amd64_10.13.0.tar.gz` from the YugabyteDB Anywhere node, as follows:

      ```sh
      cd /usr/local
      sudo tar xfz path-to-azcopy_linux_amd64_10.13.0.tar.gz -C /usr/local/bin azcopy_linux_amd64_10.13.0/azcopy --strip-components 1
      ```

- Google Cloud Storage: Install gsutil using one of the following options:

  - Download `gsutil_4.60.tar.gz` using the following command:

      ```sh
      wget https://storage.googleapis.com/pub/gsutil_4.60.tar.gz
      ```

  - For airgapped installations, copy `/opt/third-party/gsutil_4.60.tar.gz` from the YugabyteDB Anywhere node, as follows:

      ```sh
      cd /usr/local
      sudo tar xvfz gsutil_4.60.tar.gz
      sudo ln -s /usr/local/gsutil/gsutil /usr/local/bin/gsutil
      ```

## Set crontab permissions

YugabyteDB Anywhere supports performing YugabyteDB liveness checks, log file management, and core file management using cron jobs.

Note that sudo is required to set up this service.

If YugabyteDB Anywhere will be using cron jobs, ensure that the `yugabyte` user is allowed to run crontab:

- If you are using the `cron.allow` file to manage crontab access, add the `yugabyte` user to this file.
- If you are using the `cron.deny` file, remove the `yugabyte` user from this file.

If you are not using either file, no changes are required.

<!--

##### Manage liveness checks, logs, and cores

YugabyteDB Anywhere supports performing YugabyteDB liveness checks, log file management, and core file management using cron jobs or systemd services.

**Sudo is required to set up these services**

If YugabyteDB Anywhere will be using **cron jobs**, make sure the yugabyte user is allowed to run crontab. If you're using the cron.allow file to manage crontab access, add the yugabyte user to this file. If you're using the cron.deny file, remove the yugabyte user from this file.

YugabyteDB Anywhere **systemd services** to perform the monitoring operations mentioned above, then make sure ...
-->

### Install systemd-related database service unit files

As an alternative to setting crontab permissions, you can install systemd-specific database service unit files, as follows:

1. Enable the `yugabyte` user to run the following commands as sudo or root:

    ```sh
    yugabyte ALL=(ALL:ALL) NOPASSWD: \
    /bin/systemctl start yb-master, \
    /bin/systemctl stop yb-master, \
    /bin/systemctl restart yb-master, \
    /bin/systemctl enable yb-master, \
    /bin/systemctl disable yb-master, \
    /bin/systemctl start yb-tserver, \
    /bin/systemctl stop yb-tserver, \
    /bin/systemctl restart yb-tserver, \
    /bin/systemctl enable yb-tserver, \
    /bin/systemctl disable yb-tserver, \
    /bin/systemctl start yb-controller, \
    /bin/systemctl stop yb-controller, \
    /bin/systemctl restart yb-controller, \
    /bin/systemctl enable yb-controller, \
    /bin/systemctl disable yb-controller, \
    /bin/systemctl start yb-bind_check.service, \
    /bin/systemctl stop yb-bind_check.service, \
    /bin/systemctl restart yb-bind_check.service, \
    /bin/systemctl enable yb-bind_check.service, \
    /bin/systemctl disable yb-bind_check.service, \
    /bin/systemctl start yb-zip_purge_yb_logs.timer, \
    /bin/systemctl stop yb-zip_purge_yb_logs.timer, \
    /bin/systemctl restart yb-zip_purge_yb_logs.timer, \
    /bin/systemctl enable yb-zip_purge_yb_logs.timer, \
    /bin/systemctl disable yb-zip_purge_yb_logs.timer, \
    /bin/systemctl start yb-clean_cores.timer, \
    /bin/systemctl stop yb-clean_cores.timer, \
    /bin/systemctl restart yb-clean_cores.timer, \
    /bin/systemctl enable yb-clean_cores.timer, \
    /bin/systemctl disable yb-clean_cores.timer, \
    /bin/systemctl start yb-collect_metrics.timer, \
    /bin/systemctl stop yb-collect_metrics.timer, \
    /bin/systemctl restart yb-collect_metrics.timer, \
    /bin/systemctl enable yb-collect_metrics.timer, \
    /bin/systemctl disable yb-collect_metrics.timer, \
    /bin/systemctl start yb-zip_purge_yb_logs, \
    /bin/systemctl stop yb-zip_purge_yb_logs, \
    /bin/systemctl restart yb-zip_purge_yb_logs, \
    /bin/systemctl enable yb-zip_purge_yb_logs, \
    /bin/systemctl disable yb-zip_purge_yb_logs, \
    /bin/systemctl start yb-clean_cores, \
    /bin/systemctl stop yb-clean_cores, \
    /bin/systemctl restart yb-clean_cores, \
    /bin/systemctl enable yb-clean_cores, \
    /bin/systemctl disable yb-clean_cores, \
    /bin/systemctl start yb-collect_metrics, \
    /bin/systemctl stop yb-collect_metrics, \
    /bin/systemctl restart yb-collect_metrics, \
    /bin/systemctl enable yb-collect_metrics, \
    /bin/systemctl disable yb-collect_metrics, \
    /bin/systemctl daemon-reload
    ```

2. Ensure that you have root access and add the following service and timer files to the `/etc/systemd/system` directory (set their ownerships to the `yugabyte` user and 0644 permissions):

    `yb-master.service`

    ```properties
    [Unit]
    Description=Yugabyte master service
    Requires=network-online.target
    After=network.target network-online.target multi-user.target
    StartLimitInterval=100
    StartLimitBurst=10

    [Path]
    PathExists=/home/yugabyte/master/bin/yb-master
    PathExists=/home/yugabyte/master/conf/server.conf

    [Service]
    User=yugabyte
    Group=yugabyte
    # Start
    ExecStart=/home/yugabyte/master/bin/yb-master --flagfile /home/yugabyte/master/conf/server.conf
    Restart=on-failure
    RestartSec=5
    # Stop -> SIGTERM - 10s - SIGKILL (if not stopped) [matches existing cron behavior]
    KillMode=process
    TimeoutStopFailureMode=terminate
    KillSignal=SIGTERM
    TimeoutStopSec=10
    FinalKillSignal=SIGKILL
    # Logs
    StandardOutput=syslog
    StandardError=syslog
    # ulimit
    LimitCORE=infinity
    LimitNOFILE=1048576
    LimitNPROC=12000

    [Install]
    WantedBy=default.target
    ```

    `yb-tserver.service`

    ```properties
    [Unit]
    Description=Yugabyte tserver service
    Requires=network-online.target
    After=network.target network-online.target multi-user.target
    StartLimitInterval=100
    StartLimitBurst=10

    [Path]
    PathExists=/home/yugabyte/tserver/bin/yb-tserver
    PathExists=/home/yugabyte/tserver/conf/server.conf

    [Service]
    User=yugabyte
    Group=yugabyte
    # Start
    ExecStart=/home/yugabyte/tserver/bin/yb-tserver --flagfile /home/yugabyte/tserver/conf/server.conf
    Restart=on-failure
    RestartSec=5
    # Stop -> SIGTERM - 10s - SIGKILL (if not stopped) [matches existing cron behavior]
    KillMode=process
    TimeoutStopFailureMode=terminate
    KillSignal=SIGTERM
    TimeoutStopSec=10
    FinalKillSignal=SIGKILL
    # Logs
    StandardOutput=syslog
    StandardError=syslog
    # ulimit
    LimitCORE=infinity
    LimitNOFILE=1048576
    LimitNPROC=12000

    [Install]
    WantedBy=default.target
    ```

    `yb-zip_purge_yb_logs.service`

    ```properties
    [Unit]
    Description=Yugabyte logs
    Wants=yb-zip_purge_yb_logs.timer

    [Service]
    User=yugabyte
    Group=yugabyte
    Type=oneshot
    WorkingDirectory=/home/yugabyte/bin
    ExecStart=/bin/sh /home/yugabyte/bin/zip_purge_yb_logs.sh

    [Install]
    WantedBy=multi-user.target
    ```

    `yb-zip_purge_yb_logs.timer`

    ```properties
    [Unit]
    Description=Yugabyte logs
    Requires=yb-zip_purge_yb_logs.service

    [Timer]
    User=yugabyte
    Group=yugabyte
    Unit=yb-zip_purge_yb_logs.service
    # Run hourly at minute 0 (beginning) of every hour
    OnCalendar=00/1:00

    [Install]
    WantedBy=timers.target
    ```

    `yb-clean_cores.service`

    ```properties
    [Unit]
    Description=Yugabyte clean cores
    Wants=yb-clean_cores.timer

    [Service]
    User=yugabyte
    Group=yugabyte
    Type=oneshot
    WorkingDirectory=/home/yugabyte/bin
    ExecStart=/bin/sh /home/yugabyte/bin/clean_cores.sh

    [Install]
    WantedBy=multi-user.target
    ```

    `yb-controller.service`

    ```properties
    [Unit]
    Description=Yugabyte Controller
    Requires=network-online.target
    After=network.target network-online.target multi-user.target
    StartLimitInterval=100
    StartLimitBurst=10

    [Path]
    PathExists=/home/yugabyte/controller/bin/yb-controller-server
    PathExists=/home/yugabyte/controller/conf/server.conf

    [Service]
    User=yugabyte
    Group=yugabyte
    # Start
    ExecStart=/home/yugabyte/controller/bin/yb-controller-server \
        --flagfile /home/yugabyte/controller/conf/server.conf
    Restart=always
    RestartSec=5
    # Stop -> SIGTERM - 10s - SIGKILL (if not stopped) [matches existing cron behavior]
    KillMode=control-group
    TimeoutStopFailureMode=terminate
    KillSignal=SIGTERM
    TimeoutStopSec=10
    FinalKillSignal=SIGKILL
    # Logs
    StandardOutput=syslog
    StandardError=syslog
    # ulimit
    LimitCORE=infinity
    LimitNOFILE=1048576
    LimitNPROC=12000

    [Install]
    WantedBy=default.target
    ```

    `yb-clean_cores.timer`

    ```properties
    [Unit]
    Description=Yugabyte clean cores
    Requires=yb-clean_cores.service

    [Timer]
    User=yugabyte
    Group=yugabyte
    Unit=yb-clean_cores.service
    # Run every 10 minutes offset by 5 (5, 15, 25...)
    OnCalendar=*:0/10:30

    [Install]
    WantedBy=timers.target
    ```

    `yb-collect_metrics.service`

    ```properties
    [Unit]
    Description=Yugabyte collect metrics
    Wants=yb-collect_metrics.timer

    [Service]
    User=yugabyte
    Group=yugabyte
    Type=oneshot
    WorkingDirectory=/home/yugabyte/bin
    ExecStart=/bin/bash /home/yugabyte/bin/collect_metrics_wrapper.sh

    [Install]
    WantedBy=multi-user.target
    ```

    `yb-collect_metrics.timer`

    ```properties
    [Unit]
    Description=Yugabyte collect metrics
    Requires=yb-collect_metrics.service

    [Timer]
    User=yugabyte
    Group=yugabyte
    Unit=yb-collect_metrics.service
    # Run every 1 minute
    OnCalendar=*:0/1:0

    [Install]
    WantedBy=timers.target
    ```

    `yb-bind_check.service`

    ```properties
    [Unit]
    Description=Yugabyte IP bind check
    Requires=network-online.target
    After=network.target network-online.target multi-user.target
    Before=yb-controller.service yb-tserver.service yb-master.service yb-collect_metrics.timer
    StartLimitInterval=100
    StartLimitBurst=10

    [Path]
    PathExists=/home/yugabyte/controller/bin/yb-controller-server
    PathExists=/home/yugabyte/controller/conf/server.conf

    [Service]
    # Start
    ExecStart=/home/yugabyte/controller/bin/yb-controller-server \
        --flagfile /home/yugabyte/controller/conf/server.conf \
        --only_bind --logtostderr
    Type=oneshot
    KillMode=control-group
    KillSignal=SIGTERM
    TimeoutStopSec=10
    # Logs
    StandardOutput=syslog
    StandardError=syslog

    [Install]
    WantedBy=default.target
    ```

## Install node agent

The node agent is used to manage communication between YugabyteDB Anywhere and the node. YugabyteDB Anywhere uses node agents to communicate with the nodes, and once installed, YugabyteDB Anywhere no longer requires SSH or sudo access to nodes.

Node agents are installed onto instances automatically when adding instances or running the pre-provisioning script using the `--install_node_agent` flag.

You can install the YugabyteDB node agent manually. As the `yugabyte` user, do the following:

1. Download the installer from YugabyteDB Anywhere using the [API token](../../../anywhere-automation/#authentication) of the Super Admin, as follows:

   ```sh
   curl https://<yugabytedb_anywhere_address>/api/v1/node_agents/download --fail --header 'X-AUTH-YW-API-TOKEN: <api_token>' > installer.sh && chmod +x installer.sh
   ```

    To create an API token, navigate to your **User Profile** and click **Generate Key**.

1. Verify that the installer file contains the script.

1. Run the following command to download the node agent's `.tgz` file which installs and starts the interactive configuration:

   ```sh
   ./installer.sh -c install -u https://<yba_address>:9000 -t <api_token>
   ```

   For example, if you run the following:

   ```sh
   node-agent node configure -t 1ba391bc-b522-4c18-813e-71a0e76b060a -u http://10.98.0.42:9000
   ```

   You should get output similar to the following:

   ```output
   * Starting YB Node Agent install
   * Creating Node Agent Directory
   * Changing directory to node agent
   * Creating Sub Directories
   * Downloading YB Node Agent build package
   * Getting Linux/amd64 package
   * Downloaded Version - 2.17.1.0-PRE_RELEASE
   * Extracting the build package
   * The current value of Node IP is not set; Enter new value or enter to skip: 10.9.198.2
   * The current value of Node Name is not set; Enter new value or enter to skip: Test
   * Select your Onprem Provider
   1. Provider ID: 41ac964d-1db2-413e-a517-2a8d840ff5cd, Provider Name: onprem
           Enter the option number: 1
   * Select your Instance Type
   1. Instance Code: c5.large
           Enter the option number: 1
   * Select your Region
   1. Region ID: dc0298f6-21bf-4f90-b061-9c81ed30f79f, Region Code: us-west-2
           Enter the option number: 1
   * Select your Zone
   1. Zone ID: 99c66b32-deb4-49be-85f9-c3ef3a6e04bc, Zone Name: us-west-2c
           Enter the option number: 1
           • Completed Node Agent Configuration
           • Node Agent Registration Successful
   You can install a systemd service on linux machines by running sudo node-agent-installer.sh -c install_service --user yugabyte (Requires sudo access).
   ```

1. Run the `install_service` command as a sudo user:

    ```sh
    sudo node-agent-installer.sh -c install_service --user yugabyte
    ```

    This installs node agent as a systemd service. This is required so that the node agent can perform self-upgrade, database installation and configuration, and other functions.

When the installation has been completed, the configurations are saved in the `config.yml` file located in the `node-agent/config/` directory. You should refrain from manually changing values in this file.

After the installation, you may need to either sign out and back in, or edit the ~/.bashrc file as the `yugabyte` user to add the node agent binary to your PATH.

### Preflight check

After the node agent is installed, configured, and connected to YugabyteDB Anywhere, you can perform a series of preflight checks without sudo privileges by using the following command:

```sh
node-agent node preflight-check
```

The result of the check is forwarded to YugabyteDB Anywhere for validation. The validated information is posted in a tabular form on the terminal. If there is a failure against a required check, you can apply a fix and then rerun the preflight check.

Expect an output similar to the following:

![Result](/images/yp/node-agent-preflight-check.png)

If the preflight check is successful, you add the node to the provider (if required) by executing the following:

```sh
node-agent node preflight-check --add_node
```

### Reconfigure a node agent

If you need to reconfigure a node agent, you can use the following procedure:

1. If the node instance has been added to a provider, remove the node instance from the provider.

1. Run the following command:

    ```sh
    node-agent node unregister
    ```

    After running this command, YBA no longer recognizes the node agent. However, if the node agent configuration is corrupted, the command may fail. In this case, unregister the node agent using the API as follows:

    - Obtain the node agent ID:

        ```sh
        curl -k --header 'X-AUTH-YW-API-TOKEN:<api_token>' https://<yba_address>/api/v1/customers/<customer_id>/node_agents?nodeIp=<node_agent_ip>
        ```

        You should see output similar to the following:

        ```output.json
        [{
            "uuid":"ec7654b1-cf5c-4a3b-aee3-b5e240313ed2",
            "name":"node1",
            "ip":"10.9.82.61",
            "port":9070,
            "customerUuid":"f33e3c9b-75ab-4c30-80ad-cba85646ea39",
            "version":"2.18.6.0-PRE_RELEASE",
            "state":"READY",
            "updatedAt":"2023-12-19T23:56:43Z",
            "config":{
                "certPath":"/opt/yugaware/node-agent/certs/f33e3c9b-75ab-4c30-80ad-cba85646ea39/ec7654b1-cf5c-4a3b-aee3-b5e240313ed2/0",
                "offloadable":false
                },
            "osType":"LINUX",
            "archType":"AMD64",
            "home":"/home/yugabyte/node-agent",
            "versionMatched":true,
            "reachable":false
        }]
        ```

    - Use the value of the field `uuid` as `<node_agent_id>` in the following command:

        ```sh
        curl -k -X DELETE --header 'X-AUTH-YW-API-TOKEN:<api_token>' https://<yba_address>/api/v1/customers/<customer_id>/node_agents/<node_agent_id>
        ```

1. Stop the systemd service as a sudo user.

    ```sh
    sudo systemctl stop yb-node-agent
    ```

1. Run the `configure` command to start the interactive configuration. This also registers the node agent with YBA.

    ```sh
    node-agent node configure -t <api_token> -u https://<yba_address>:9000
    ```

    For example, if you run the following:

    ```sh
    ./installer.sh  -c install -u http://10.98.0.42:9000 -t 301fc382-cf06-4a1b-b5ef-0c8c45273aef
    ```

    ```output
    * The current value of Node Name is set to node1; Enter new value or enter to skip: 
    * The current value of Node IP is set to 10.9.82.61; Enter new value or enter to skip: 
    * Select your Onprem Provider.
    1. Provider ID: b56d9395-1dda-47ae-864b-7df182d07fa7, Provider Name: onprem-provision-test1
    * The current value is Provider ID: b56d9395-1dda-47ae-864b-7df182d07fa7, Provider Name: onprem-provision-test1.
        Enter new option number or enter to skip: 
    * Select your Instance Type.
    1. Instance Code: c5.large
    * The current value is Instance Code: c5.large.
        Enter new option number or enter to skip: 
    * Select your Region.
    1. Region ID: 0a185358-3de0-41f2-b106-149be3bf07dd, Region Code: us-west-2
    * The current value is Region ID: 0a185358-3de0-41f2-b106-149be3bf07dd, Region Code: us-west-2.
        Enter new option number or enter to skip: 
    * Select your Zone.
    1. Zone ID: c9904f64-a65b-41d3-9afb-a7249b2715d1, Zone Code: us-west-2a
    * The current value is Zone ID: c9904f64-a65b-41d3-9afb-a7249b2715d1, Zone Code: us-west-2a.
        Enter new option number or enter to skip: 
    • Completed Node Agent Configuration
    • Node Agent Registration Successful
    ```

1. Start the Systemd service as a sudo user.

    ```sh
    sudo systemctl start yb-node-agent
    ```

1. Verify that the service is up.

    ```sh
    sudo systemctl status yb-node-agent
    ```

1. Run preflight checks and add the node as `yugabyte` user.

    ```sh
    node-agent node preflight-check --add_node
    ```
