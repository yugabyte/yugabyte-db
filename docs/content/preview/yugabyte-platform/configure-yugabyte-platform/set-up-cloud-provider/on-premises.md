---
title: Configure the on-premises cloud provider
headerTitle: Configure the on-premises cloud provider
linkTitle: Configure the cloud provider
description: Configure the on-premises cloud provider.
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/onprem
menu:
  preview:
    identifier: set-up-cloud-provider-6-on-premises
    parent: configure-yugabyte-platform
    weight: 20
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="../openshift/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link active">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

You can configure the on-premises cloud provider for YugabyteDB using the Yugabyte Platform console. If no cloud providers are configured, the main Dashboard prompts you to configure at least one cloud provider, as per the following illustration:

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-0.png)

## Configure the on-premises provider

Configuring the on-premises provided consists of a number of steps.

### Complete the provider information {#on-premise-provider-info}

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-1.png)

#### Provider Name

Supply the provider name, which is an internal tag that helps with organizing your providers, so you know where you want to deploy your YugabyteDB universes.

#### SSH User

To provision on-premises nodes with YugabyteDB, Yugabyte Platform requires SSH access to these nodes. Unless you plan to provision the database nodes manually, the user needs to have password-free sudo permissions to complete a few tasks.

If the SSH user requires a password for sudo access or the SSH user does not have sudo access, follow the steps described in [Manually Provision Nodes](#manually-provision-nodes).

#### SSH Port

Provide the port number of SSH client connections.

#### Manually Provision Nodes

Enable this option if you choose to manually set up your database nodes. Otherwise, Yugabyte Platform will use the sudo user to set up YugabyteDB nodes. For manual provisioning, you would be prompted to run a Python script at a later stage or to run a set of commands on the database nodes.

If any of the following statements are applicable to your use case, you need to [provision the nodes manually](#provision-nodes-manually):

* Pre-provisioned `yugabyte:yugabyte` user and group.
* Sudo user requires a password.
* The SSH user is not a sudo user.

#### SSH Key

Ensure that the SSH key is pasted correctly (the supported format is RSA).

#### Air Gap Install

Enable this option if you want the installation to run in an air-gapped mode without expecting any internet access.

#### Desired Home Directory

Optionally, specify the home directory of the `yugabyte` user. The default value is `/home/yugabyte`.

#### Node Exporter Port

Specify the port number for the Node Exporter. The default value is 9300.

#### Install Node Exporter

Enable this option if you want the Node Exporter installed. You can skip this step if you have Node Exporter already installed on the nodes. Ensure you have provided the correct port number for skipping the installation.

#### Node Exporter User

Override the default Prometheus user. This is useful when the user is pre-provisioned on nodes (in case user creation is disabled). If overridden, the installer checks whether or not the user exists and creates the user if it does not exist.

### Configure hardware for YugabyteDB nodes

Complete the **Instance Types** fields, as per the following illustration, to provide node hardware configuration (CPU, memory, and volume information):

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-2.png)

#### Machine Type

Define a value to be used internally as an identifier in the **Instance Type** universe field.

#### Num Cores

Define the number of cores to be assigned to a node.

#### Mem Size GB

Define the memory allocation of a node.

#### Vol size GB

Define the disk volume of a node.

#### Mount Paths

Define a mount point with enough space to contain your node density. Use `/data`. If you have multiple drives, add these as a comma-separated list, such as, for example, `/mnt/d0,/mnt/d1`.

### Define regions and zones

Complete the **Regions and Zones** fields, as per in the following illustration, to provide the location of YugabyteDB nodes. Yugabyte Platform will use these values during the universe creation:

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-3.png)

## Add YugabyteDB nodes

After finishing the provider configuration, click **Manage Instances** to provision as many nodes as your application requires.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-4.png)

For each node you want to add, click **Add Instances** to add a YugabyteDB node. You can use DNS names or IP addresses when adding instances (instance ID is an optional user-defined identifier).

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-5.png)

<br>Note that if you provide a hostname, the universe might experience issues communicating. To resolve this, you need to delete the failed universe and then recreate it with the `use_node_hostname_for_local_tserver ` g-flag enabled.

### Provision nodes manually

To provision your nodes manually, you have the following two options:

1. If the SSH user you provided has sudo privileges but requires a password, you can [run the pre-provisioning script](#running-the-pre-provisioning-script).
2. If the SSH user does not have any sudo privileges, you need to [set up the database nodes manually](#setting-up-database-nodes-manually).

#### Running the pre-provisioning script

This step is only required if you set **Manually Provision Nodes** to true and the SSH user has sudo privileges which require a password; otherwise you skip this step.

You can manually provision each node using the pre-provisioning Python script, as follows:

1. Login to Yugabyte Platform virtual machine via SSH.

1. Access the docker `yugaware` container, as follows:

    ```sh
    sudo docker exec -it yugaware bash
    ```

1. Copy and paste the Python script prompted via the UI and substitute for a node IP address and mount points.
Optionally, use the `--ask_password` flag if the sudo user requires password authentication, as follows:

    ```bash
   bash-4.4# /opt/yugabyte/yugaware/data/provision/9cf26f3b-4c7c-451a-880d-593f2f76efce/provision_instance.py --ip 10.9.116.65 --mount_points /data --ask_password
    ```

   Expect the following output and prompt:

    ```output
    Executing provision now for instance with IP 10.9.116.65...
    SUDO password:
    ```

1. Wait for the script to finish successfully.

1. Repeat step 3 for every node that will participate in the universe.

This completes the on-premises cloud provider configuration. You can proceed to [Configure the backup target](../../backup-target/) or [Create deployments](../../../create-deployments/).

#### Setting up database nodes manually

This step is only required if you set **Manually Provision Nodes** to true and the SSH user does not have sudo privileges at all; otherwise you skip this step.

If the SSH user configured in the on-premises provider does not have sudo privileges, then you can set up each of the database nodes manually. Note that you need access to a user with sudo privileges in order to complete these steps.

For each node, perform the following:

* [Set up time synchronization](#set-up-time-synchronization)
* [Open incoming TCP ports](#open-incoming-tcp-ip-ports)
* [Pre-provision the node](#pre-provision-nodes-manually)
* [Install Prometheus node exporter](#install-prometheus-node-exporter)
* [Install backup utilities](#install-backup-utilities)
* [Set crontab permissions](#set-crontab-permissions)

##### Set up time synchronization

A local Network Time Protocol (NTP) server or equivalent must be available.

Ensure an NTP-compatible time service client is installed in the node OS (chrony is installed by default in the standard CentOS 7 instance used in this example). Then, configure the time service client to use the available time server. The procedure includes this step and assumes chrony is the installed client.

##### Open incoming TCP/IP ports

Database servers need incoming TCP/IP access enabled to the following ports, for communications between themselves and Yugabyte Platform:

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

The preceding table is based on the information on the [default ports page](/preview/reference/configuration/default-ports/).

##### Pre-provision nodes manually

This process carries out all provisioning tasks on the database nodes which require elevated privileges. Once the database nodes have been prepared in this way, the universe creation process from Yugabyte Platform will connect with the nodes only via the `yugabyte` user, and not require any elevation of privileges to deploy and operate the YugabyteDB universe.

Physical nodes (or cloud instances) are installed with a standard Centos 7 server image. The following steps are to be performed on each physical node, prior to universe creation:

1. Login to each database node as a user with sudo enabled (the `centos` user in centos7 images).

1. Add the following line to `/etc/chrony.conf` (sudo is required):

    ```text
    server <your-time-server-IP-address> prefer iburst
    ```

    <br>Then, run the following command:

    ```sh
    sudo chronyc makestep   # (force instant sync to NTP server)
    ```

1. Add a new `yugabyte:yugabyte` user and group (sudo is required):

    ```sh
    sudo useradd yugabyte   # (add group yugabyte + create /home/yugabyte)
    sudo passwd yugabyte   # (add a password to the yugabyte user)
    sudo su - yugabyte   # (change to yugabyte user for execution of next steps)
    ```

    <br>Ensure that the `yugabyte` user has permissions to SSH into the YugabyteDB nodes (as defined in `/etc/ssh/sshd_config`).

1. Copy the SSH public key to each DB node.

    \
    This public key should correspond to the private key entered into the Yugabyte Platform provider.

1. Run the following commands as the `yugabyte` user, after copying the SSH public key file to the user home directory:

    ```sh
    cd ~yugabyte
    mkdir .ssh
    chmod 700 .ssh
    cat <pubkey file> >> .ssh/authorized_keys
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

1. Modify the following line in the `/etc/security/limits.d/20-nproc.conf` file (sudo is required):

    ```text
    *          soft    nproc     12000
    ```

1. Install the rsync and OpenSSL packages (sudo is required).

    \
    Note that most Linux distributions include rsync and OpenSSL. If your distribution is missing these packages, install them using the following commands:

    ```sh
    sudo yum install openssl
    sudo yum install rsync
    ```

    \
    For airgapped environments, make sure your Yum repository mirror contains these packages.

1. If running on a virtual machine, execute the following to tune kernel settings (sudo is required):

    ```sh
    sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
    sysctl kernel.core_pattern=/home/yugabyte/cores/core_%e.%p >> /etc/sysctl.conf
    ```

1. Perform the following to prepare and mount the data volume (separate partition for database data) (sudo is required):

    * List the available storage volumes, as follows:

      ```sh
      lsblk
      ```

    * Perform the following steps for each available volume (all listed volumes other than the root volume):

      ```sh
      sudo mkdir /data   # (or /data1, /data2 etc)
      sudo mkfs -t xfs /dev/nvme1n1   # (create xfs filesystem over entire volume)
      sudo vi /etc/fstab
      ```

    * Add the following line to `/etc/fstab`:

      ```text
      /dev/nvme1n1   /data   xfs   noatime   0   0
      ```

    * Exit from vi, and continue, as follows:

      ```sh
      sudo mount -av # (mounts the new volume using the fstab entry, to validate)
      sudo chown yugabyte:yugabyte /data
      sudo chmod 755 /data
      ```

##### Install Prometheus node exporter

For Yugabyte Platform versions 2.8 and later, download the 1.3.1 version of the Prometheus node exporter, as follows:

```sh
wget https://github.com/prometheus/node_exporter/releases/download/v1.3.1/node_exporter-1.3.1.linux-amd64.tar.gz
```

For Yugabyte Platform versions prior to 2.8, download the 0.13.0 version of the exporter, as follows:

```sh
$ wget https://github.com/prometheus/node_exporter/releases/download/v0.13.0/node_exporter-0.13.0.linux-amd64.tar.gz
```

If you are doing an airgapped installation, download the node exporter using a computer connected to the internet and copy it over to the database nodes.

Note that the instructions are for the 0.13.0 version. The same instructions are applicable to the 1.3.1 version, but you need to use the correct file name.

On each node, perform the following as a user with sudo access:

1. Copy the `node_exporter-....tar.gz` package file that you downloaded into the `/tmp` directory on each of the YugabyteDB nodes. Ensure this file is readable by the `centos` user on each node (or another user with sudo privileges).

1. Run the following commands (sudo required):

    ```sh
    sudo mkdir /opt/prometheus
    sudo mkdir /etc/prometheus
    sudo mkdir /var/log/prometheus
    sudo mkdir /var/run/prometheus
    sudo mv /tmp/node_exporter-0.13.0.linux-amd64.tar  /opt/prometheus
    sudo adduser prometheus # (also adds group “prometheus”)
    sudo chown -R prometheus:prometheus /opt/prometheus
    sudo chown -R prometheus:prometheus /etc/prometheus
    sudo chown -R prometheus:prometheus /var/log/prometheus
    sudo chown -R prometheus:prometheus /var/run/prometheus
    sudo chmod +r /opt/prometheus/node_exporter-0.13.0.linux-amd64.tar
    sudo su - prometheus (user session is now as user “prometheus”)
    ```

1. Run the following commands as user `prometheus`:

    ```sh
    cd /opt/prometheus
    tar zxf node_exporter-0.13.0.linux-amd64.tar.gz
    exit   # (exit from prometheus user back to previous user)
    ```

1. Edit the following file (sudo required):

    ```sh
    sudo vi /etc/systemd/system/node_exporter.service
    ```

    \
    Add the following to `/etc/systemd/system/node_exporter.service`:

      ```conf
      [Unit]
      Description=node_exporter - Exporter for machine metrics.
      Documentation=https://github.com/William-Yeh/ansible-prometheus
      After=network.target

      [Install]
      WantedBy=multi-user.target

      [Service]
      Type=simple

      #ExecStartPre=/bin/sh -c  " mkdir -p '/var/run/prometheus' '/var/log/prometheus' "
      #ExecStartPre=/bin/sh -c  " chown -R prometheus '/var/run/prometheus' '/var/log/prometheus' "
      #PIDFile=/var/run/prometheus/node_exporter.pid

      User=prometheus
      Group=prometheus

      ExecStart=/opt/prometheus/node_exporter-0.13.0.linux-amd64/node_exporter  --web.listen-address=:9300 --collector.textfile.directory=/tmp/yugabyte/metrics
      ```

1. Exit from vi, and continue, as follows (sudo required):

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

Yugabyte Platform supports backing up YugabyteDB to AWS S3, Azure Storage, Google Cloud Storage, and NFS.

You can install the backup utility for the backup storage you plan to use as follows:

- NFS - Install rsync. Yugabyte Platform uses rsync to do NFS backups which you installed in an earlier step.

- AWS S3 - Install s3cmd. Yugabyte Platform relies on s3cmd to support copying backups to AWS S3. You have the following installation options:
  - For a regular installation, execute the following:

      ```sh
      sudo yum install s3cmd
      ```

  - For an airgapped installation, copy `/opt/third-party/s3cmd-2.0.1.tar.gz` from the Yugabyte Platform node to the database node, and then extract it into the `/usr/local` directory on the database node, as follows:

      ```sh
      cd /usr/local
      sudo tar xvfz path-to-s3cmd-2.0.1.tar.gz
      sudo ln -s /usr/local/s3cmd-2.0.1/s3cmd /usr/local/bin/s3cmd
      ```

- Azure Storage - Install azcopy using one of the following options:
  - Download `azcopy_linux_amd64_10.13.0.tar.gz` using the following command:

      ```sh
      wget https://azcopyvnext.azureedge.net/release20200410/azcopy_linux_amd64_10.13.0.tar.gz
      ```

  - For airgapped installations, copy `/opt/third-party/azcopy_linux_amd64_10.13.0.tar.gz` from the Yugabyte Platform node, as follows:

      ```sh
      cd /usr/local
      sudo tar xfz path-to-azcopy_linux_amd64_10.13.0.tar.gz -C /usr/local/bin azcopy_linux_amd64_10.13.0/azcopy --strip-components 1
      ```

- Google Cloud Storage - Install gsutil using one of the following options:
  - Download `gsutil_4.60.tar.gz` using the following command:

      ```sh
      wget https://storage.googleapis.com/pub/gsutil_4.60.tar.gz
      ```

  - For airgapped installs, copy `/opt/third-party/gsutil_4.60.tar.gz` from the Yugabyte Platform node, as follows:

      ```sh
      cd /usr/local
      sudo tar xvfz gsutil_4.60.tar.gz
      sudo ln -s /usr/local/gsutil/gsutil /usr/local/bin/gsutil
      ```

##### Set crontab permissions

Yugabyte Platform supports performing YugabyteDB liveness checks, log file management, and core file management using cron jobs.

Note that sudo is required to set up this service.

If Yugabyte Platform will be using cron jobs, ensure that the `yugabyte` user is allowed to run crontab:

* If you are using the `cron.allow` file to manage crontab access, add the `yugabyte` user to this file.
* If you are using the `cron.deny` file, remove the `yugabyte` user from this file.

If you are not using either file, no changes are required.

<!--

##### Manage liveness checks, logs, and cores

Yugabyte Platform supports performing YugabyteDB liveness checks, log file management, and core file management using cron jobs or systemd services.

**Sudo is required to set up these services!**

If Platform will be using **cron jobs**, make sure the yugabyte user is allowed to run crontab. If you're using the cron.allow file to manage crontab access, add the yugabyte user to this file. If you're using the cron.deny file, remove the yugabyte user from this file.

If you plan to have Platform use **systemd services** to perform the monitoring operations mentioned above, then make sure ...
-->

You have finished configuring your on-premises cloud provider. Proceed to [Configure the backup target](../../backup-target/), or [Create deployments](../../../create-deployments/).

## Remove YugabyteDB components from the server

As described in [Eliminate an unresponsive node](../../../manage-deployments/remove-nodes/), when a node enters an undesirable state, you can delete such node, with Yugabyte Platform clearing up all the remaining artifacts except the `prometheus` and `yugabyte` user.

You can manually remove Yugabyte components from existing server images. Before attempting this, you have to determine whether or not Yugaware Platform is operational. If it is, you either need to delete the universe or delete the nodes from the universe.

In order to completely eliminate all traces of Yugabyte Platform and configuration, you should consider reinstalling the operating system image (or rolling back to a previous image, if available).

### Delete database server nodes

You can remove YugabyteDB components and configuration from the database server nodes as follows:

- Login to the server node as the `yugabyte` user.

- Navigate to the `/home/yugabyte/bin` directory that contains a number of scripts including `yb-server-ctl.sh`. The arguments set in this script allow you to perform various functions on the YugabyteDB processes running on the node.

- Execute the following command:

  ```shell
  ./bin/yb-server-ctl.sh clean-instance
  ```

  <br>This removes all YugabyteDB code and settings from the node, removing it from the Universe.

{{< note title="Note" >}}

If you cannot find the `bin` directory, it means Yugabyte Platform already cleared it during a successful deletion of the universe.

{{< /note >}}

You shoud also erase the data from the volume mounted under the `/data` subdirectory, unless this volume is to be permanently erased by the underlying storage subsystem when the volume is deleted.

To erase this data, execute the following commands from the `centos` user on the node (or any user with access to sudo):

```sh
sudo umount /data
```

```sh
sudo dd if=/dev/zero of=/dev/sdb bs=1M
```

The preceding commands assume the data volume is attached to the server as `/dev/sdb`.

If there is a requirement to remove the `yugabyte` user, execute the following command:

```sh
sudo userdel -r yugabyte
```

If there is a requirement to remove the `prometheus` user, execute the following command:

```sh
sudo rm -rf /opt/prometheus
```

You may now choose to reverse the system settings that you configured in [Provision nodes manually](#provision-nodes-manually).

### Delete Yugabyte Platform from the server

To remove Yugabyte Platform and Replicated components from the host server, execute the following commands as the `root` user (or prepend `sudo` to each command) :

```sh
systemctl stop replicated replicated-ui replicated-operator
service replicated stop
service replicated-ui stop
service replicated-operator stop
docker stop replicated-premkit
docker stop replicated-statsd
```

```sh
docker rm -f replicated replicated-ui replicated-operator \ replicated-premkit replicated-statsd retraced-api retraced-processor \ retraced-cron retraced-nsqd retraced-postgres
```

```sh
docker images | grep "quay.io/replicated" | awk '{print $3}' | xargs sudo docker rmi -f
```

```sh
docker images | grep "registry.replicated.com/library/retraced" | awk '{print $3}' | xargs sudo docker rmi -f
```

```sh
yum remove -y replicated replicated-ui replicated-operator
```

```sh
rm -rf /var/lib/replicated* /etc/replicated* /etc/init/replicated* \ /etc/default/replicated* /etc/systemd/system/replicated* \ /etc/sysconfig/replicated* \ /etc/systemd/system/multi-user.target.wants/replicated* \ /run/replicated*
```

```sh
rpm -qa | grep -i docker
yum remove docker-ce
rpm -qa | grep -i docker
yum remove docker-ce-cli
```

