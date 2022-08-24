---
title: Configure the on-premises cloud provider
headerTitle: Configure the on-premises cloud provider
linkTitle: Configure the cloud provider
description: Configure the on-premises cloud provider.
menu:
  v2.8_yugabyte-platform:
    identifier: set-up-cloud-provider-6-on-premises
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/vmware-tanzu" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/openshift" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises" class="nav-link active">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

This page details how to configure the Onprem cloud provider for YugabyteDB using the Yugabyte Platform console. If no cloud providers are configured, the main Dashboard page highlights that you need to configure at least one cloud provider.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-0.png)

## Step 1. Configure the on-premises provider

### On-premise Provider Info {#on-premise-provider-info}

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-1.png)

#### Provider name

This is an internal tag used for organizing your providers, so you know where you want to deploy your YugabyteDB universes.

#### SSH User

To provision on-prem nodes with YugabyteDB, the Yugabyte Platform requires SSH access to these nodes. Unless you plan to provision the database nodes manually, this user needs to have _passwordless sudo permissions_ to complete a few tasks.

If the SSH user requires a password for sudo access **or** the SSH user does not have sudo access, follow the steps in the [Manually provision nodes](#manually-provision-nodes) section.

#### SSH Port

Port number of ssh client connections.

#### Manually Provision Nodes

If you choose to manually set up your database nodes, set this flag to true. Otherwise, the Yugabyte Platform will use the sudo user to set up DB nodes. For manual provisioning, you'll be prompted to run a python script at a later stage, or to run a set of commands on the database nodes.

{{< note title="Note" >}}
If any of the items from this checklist are true, you need to [provision the nodes manually](#provision-nodes-manually).

* Pre-provisioned `yugabyte:yugabyte` user + group
* Sudo user requires a password
* The SSH user is **not** a sudo user
{{< /note >}}

#### SSH Key

Ensure that the SSH key is pasted correctly (Supported format is RSA).

#### Air Gap install

If enabled, the installation will run in an air-gapped mode without expecting any internet access.

#### Use Hostnames

Indicates if nodes are expected to use DNS or IP addresses. If enabled, then all internal communication will use DNS resolution.

#### Desired Home Directory (Optional)

Specifies the home directory of yugabyte user. The default value is /home/yugabyte.

#### Node Exporter Port

This is the port number (default value 9300) for the Node Exporter. You can override this to specify a different port.

#### Install Node Exporter

Whether to install or skip installing Node Exporter. You can skip this step if you have Node Exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.

#### Node Exporter User

You can override the default prometheus user. This is useful when a user is pre-provisioned (in case user creation is disabled) on nodes. If overridden, the installer will check if the user exists and will create the user if it doesn't.

### Provision the YugabyteDB nodes

Follow the steps below to provide node hardware configuration (CPU, memory, and volume information)

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-2.png)

#### Machine Type

This is an internal user-defined tag used as an identifier in the “Instance Type” universe field.

#### Number of cores

This is the number of cores assigned to a node.

#### Mem Size (GB)

This is the memory allocation of a node.

#### Vol size (GB)

This is the disk volume of a node.

#### Mount Paths

For mount paths, use a mount point with enough space to contain your node density. Use `/data`. If you have multiple drives, add these as a comma-separated list, such as: `/mnt/d0,/mnt/d1`.

### Region and Zones

Follow the steps below to provide the location of DB nodes. All these fields are user-defined, which will be later used during the universe creation.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-3.png)

## Step 2. Provision the YugabyteDB nodes

After finishing the cloud provider configuration, click Manage Instances to provision as many nodes as your application requires.

### Add nodes

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-4.png)

**For each node you want to add**, click Add Instances to add a YugabyteDB node. You can use DNS names or IP addresses when adding instances. (Instance ID is an optional user-defined identifier.)

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-5.png)

### Provision nodes manually

To provision your nodes manually, you have two options:

* If the SSH user you provided has sudo privileges but _requires a password_, you can [run the pre-provisioning script](#run-the-pre-provisioning-script).

* If the SSH user doesn't have sudo privileges at all, you need to [set the database nodes up manually](#set-up-database-nodes-manually).

#### Run the pre-provisioning script

{{< note title="Note" >}}
This step is only required if you set Manually Provision Nodes to true **and** the SSH user has sudo privileges which require a password; otherwise, skip this step.
{{< /note >}}

Follow these steps to manually provision each node using the pre-provisioning Python script.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-6.png)

1. Log into the Platform virtual machine via SSH.

1. Access the docker `yugaware` container.

    ```sh
    $ sudo docker exec -it yugaware bash
    ```

1. Copy and paste the Python script prompted in the UI and substitute for a node IP address and mount points.

    \
    (Optional) Use the `--ask_password` flag if the sudo user requires password authentication.

    ```output
    bash-4.4# /opt/yugabyte/yugaware/data/provision/9cf26f3b-4c7c-451a-880d-593f2f76efce/provision_instance.py --ip 10.9.116.65 --mount_points /data --ask_password
    Executing provision now for instance with IP 10.9.116.65...
    SUDO password:
    ```

1. Wait for the script to finish with SUCCESS status.

1. Repeat step 3 for every node that will participate in the universe.

**You’re finished configuring your on-premises cloud provider.** Proceed to [Configure the backup target](../../backup-target/), or [Create deployments](../../../create-deployments/).

#### Set up database nodes manually

{{< note title="Note" >}}
This step is only required if you set Manually Provision Nodes to true **and** the SSH user doesn't have sudo privileges at all; otherwise, skip this step.
{{< /note >}}

If the SSH user configured in the Onprem Provider does not have sudo privileges, then set up each of the database nodes manually by following the steps in this section. Note that you will need access to a user with sudo privileges in order to complete these steps.

You'll need to do the following for each node:

* [Set up time synchronization](#set-up-time-synchronization)
* [Open incoming TCP ports](#open-incoming-tcp-ip-ports)
* [Pre-provision the node](#pre-provision-nodes-manually)
* [Install Prometheus node exporter](#install-prometheus-node-exporter)
* [Install backup utilities](#install-backup-utilities)
* [Set crontab permissions](#set-crontab-permissions)
* [Install systemd-related database service unit files (optional)](#install-systemd-related-database-service-unit-files)

##### Set up time synchronization

A local NTP server or equivalent must be available.

Ensure an NTP-compatible time service client is installed in the node OS (chrony is installed by default in the standard CentOS 7 instance used in this example). Then, configure the time service client to use the available time server. The procedure here includes this step, and assumes chrony is the installed client.

##### Open incoming TCP/IP ports

Database servers need incoming TCP/IP access enabled to the following ports, for communications between themselves and the Platform server:

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

This table is based on the information on the [default ports page](/preview/reference/configuration/default-ports/).

##### Pre-provision nodes manually

This process carries out all provisioning tasks on the database nodes which require elevated privileges. Once the database nodes have been prepared in this way, the Universe creation process from the Platform server will connect with the nodes only via the `yugabyte` user, and not require any elevation of privileges to deploy and operate the YugabyteDB Universe.

Physical nodes (or cloud instances) are installed with a standard Centos 7 server image. The following steps are to be followed on each physical node, prior to universe creation:

1. Log into each database node as a user with sudo enabled (the “centos” user in centos7 images)

1. (SUDO NEEDED) Add the following line to `/etc/chrony.conf`:

    ```text
    server <your-time-server-IP-address> prefer iburst
    ```

    \
    Then, run the following command:

    ```sh
    $ sudo chronyc makestep   # (force instant sync to NTP server)
    ```

1. (SUDO NEEDED) Add a new `yugabyte:yugabyte` user and group.

    ```sh
    $ sudo useradd yugabyte   # (add group yugabyte + create /home/yugabyte)
    $ sudo passwd yugabyte   # (add a password to the yugabyte user)
    $ sudo su - yugabyte   # (change to yugabyte user for convenient execution of next steps)
    ```

1. Copy the SSH public key to each DB node.

    \
    This public key should correspond to the private key entered into the Platform Provider elsewhere in this document.

1. Run the following commands as the `yugabyte` user, after copying the SSH public key file to the user home directory:

    ```sh
    $ cd ~yugabyte
    $ mkdir .ssh
    $ chmod 700 .ssh
    $ cat <pubkey file> >> .ssh/authorized_keys
    $ chmod 400 .ssh/authorized_keys
    $ exit   # (exit from the yugabyte user back to previous user)
    ```

1. (SUDO NEEDED) Add the following lines to `/etc/security/limits.conf`:

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

1. (SUDO NEEDED) Modify the following line in `/etc/security/limits.d/20-nproc.conf`:

    ```text
    *          soft    nproc     12000
    ```

1. (SUDO NEEDED) Install the rsync and OpenSSL packages.

    \
    Most Linux distributions include rsync and openssl. If your distribution is missing these packages, install them using the following commands:

    ```sh
    $ sudo yum install openssl
    $ sudo yum install rsync
    ```

    \
    For airgapped environments, make sure your yum repository mirror contains these packages.

1. (SUDO NEEDED) Tune kernel settings (_only if running on a Virtual machine_).

    ```sh
    $ sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
    $ sysctl kernel.core_pattern=/home/yugabyte/cores/core_%e.%p >> /etc/sysctl.conf
    ```

1. (SUDO NEEDED) Prepare and mount the data volume (separate partition for database data):

    * List the available storage volumes:

      ```sh
      $ lsblk
      ```

    * Perform the following steps **for each available volume** (all listed volumes other than the root volume):

      ```sh
      $ sudo mkdir /data   # (or /data1, /data2 etc)
      $ sudo mkfs -t xfs /dev/nvme1n1   # (create xfs filesystem over entire volume)
      $ sudo vi /etc/fstab
      ```

    * Add the following line to `/etc/fstab`:

      ```text
      /dev/nvme1n1   /data   xfs   noatime   0   0
      ```

    * Exit from vi, and continue:

      ```sh
      $ sudo mount -av (mounts the new volume using the fstab entry, to validate)
      $ sudo chown yugabyte:yugabyte /data
      $ sudo chmod 755 /data
      ```

##### Install Prometheus node exporter

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
   sudo mv /tmp/node_exporter-1.3.1.linux-amd64.tar  /opt/prometheus
   sudo adduser prometheus # (also adds group “prometheus”)
   sudo chown -R prometheus:prometheus /opt/prometheus
   sudo chown -R prometheus:prometheus /etc/prometheus
   sudo chown -R prometheus:prometheus /var/log/prometheus
   sudo chown -R prometheus:prometheus /var/run/prometheus
   sudo chmod +r /opt/prometheus/node_exporter-1.3.1.linux-amd64.tar
   sudo su - prometheus (user session is now as user “prometheus”)
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

##### Install backup utilities

Platform supports backing up YugabyteDB to AWS S3, Azure Storage, Google Cloud Storage, and NFS. Install the backup utility for the backup storage you plan to use.

**NFS**: Install rsync. Platform uses rsync to do NFS backups, which you installed in an earlier step.

**AWS S3**: Install s3cmd. Platform relies on s3cmd to support copying backups to AWS S3. You have two options to install:

* For a regular install:

    ```sh
    $ sudo yum install s3cmd
    ```

* For an airgapped install, copy `/opt/third-party/s3cmd-2.0.1.tar.gz` from the Platform node to the database node, and extract it into the `/usr/local` directory on the database node.

    ```sh
    $ cd /usr/local
    $ sudo tar xvfz path-to-s3cmd-2.0.1.tar.gz
    $ sudo ln -s /usr/local/s3cmd-2.0.1/s3cmd /usr/local/bin/s3cmd
    ```

**Azure Storage**: Install azcopy. You have two options:

* Download azcopy_linux_amd64_10.13.0.tar.gz using this command:

    ```sh
    $ wget https://azcopyvnext.azureedge.net/release20211027/azcopy_linux_amd64_10.13.0.tar.gz
    ```

* For airgapped installs, copy `/opt/third-party/azcopy_linux_amd64_10.13.0.tar.gz` from the Platform node.

    ```sh
    $ cd /usr/local
    $ sudo tar xfz path-to-azcopy_linux_amd64_10.13.0.tar.gz -C /usr/local/bin azcopy_linux_amd64_10.13.0/azcopy --strip-components 1
    ```

**Google Cloud Storage**: Install gsutil. You have two options:

* Download gsutil_4.60.tar.gz using the following command:

    ```sh
    $ wget https://storage.googleapis.com/pub/gsutil_4.60.tar.gz
    ```

* For airgapped installs, copy `/opt/third-party/gsutil_4.60.tar.gz` from the Platform node:

    ```sh
    $ cd /usr/local
    $ sudo tar xvfz gsutil_4.60.tar.gz
    $ sudo ln -s /usr/local/gsutil/gsutil /usr/local/bin/gsutil
    ```

##### Set crontab permissions

Platform supports performing yugabyte database liveness checks, log file management, and core file management using cron jobs.

**Sudo is required to set up this service!**

If Platform will be using **cron jobs**, make sure the yugabyte user is allowed to run crontab:

* If you’re using the `cron.allow` file to manage crontab access, add the yugabyte user to this file.
* If you’re using the `cron.deny` file, remove the yugabyte user from this file.

(And if you’re not using either file, no changes are required.)

<!--
##### Manage liveness checks, logs, and cores

Yugabyte Platform supports performing YugabyteDB liveness checks, log file management, and core file management using cron jobs or systemd services.

**Sudo is required to set up these services!**

If Platform will be using **cron jobs**, make sure the yugabyte user is allowed to run crontab. If you’re using the cron.allow file to manage crontab access, add the yugabyte user to this file. If you’re using the cron.deny file, remove the yugabyte user from this file.

If you plan to have Platform use **systemd services** to perform the monitoring operations mentioned above, then make sure ...
-->

**You’re finished configuring your on-premises cloud provider.** Proceed to [Configure the backup target](../../backup-target/), or [Create deployments](../../../create-deployments/).

##### Install systemd-related database service unit files

As an alternative to setting crontab permissions, you can install systemd-specific database service unit files, as follows:

1. Enable the `yugabyte` user to run the following commands as sudo or root:

   ```sh
   yugabyte ALL=(ALL:ALL) NOPASSWD: /bin/systemctl start yb-master, \
   /bin/systemctl stop yb-master, \
   /bin/systemctl restart yb-master, \
   /bin/systemctl enable yb-master, \
   /bin/systemctl disable yb-master, \
   /bin/systemctl start yb-tserver, \
   /bin/systemctl stop yb-tserver, \
   /bin/systemctl restart yb-tserver, \
   /bin/systemctl enable yb-tserver, \
   /bin/systemctl disable yb-tserver, \
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

2. Ensure that you have root access and add the following service and timer files to the `/etc/systemd/system` directory (set their ownerships to the `yugabyte` user and 0644 permissions):<br><br>

   `yb-master.service`

   ```sh
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

   <br><br>

   `yb-tserver.service`

   ```sh
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

   <br><br>`yb-zip_purge_yb_logs.service`

   ```sh
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

   <br><br>

   `yb-zip_purge_yb_logs.timer`

   ```sh
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

   <br><br>

   `yb-clean_cores.service`

   ```sh
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

   <br><br>

   `yb-clean_cores.timer`

   ```sh
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

   <br><br>

   `yb-collect_metrics.service`

   ```sh
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

   <br><br>

   `yb-collect_metrics.timer`

   ```sh
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

##
