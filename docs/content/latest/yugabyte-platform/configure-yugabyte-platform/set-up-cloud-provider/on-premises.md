---
title: Configure the on-premises cloud provider
headerTitle: Configure the on-premises cloud provider
linkTitle: Configure the cloud provider
description: Configure the on-premises cloud provider.
aliases:
  - /latest/deploy/enterprise-edition/configure-cloud-providers/onprem
menu:
  latest:
    identifier: set-up-cloud-provider-6-on-premises
    parent: configure-yugabyte-platform
    weight: 20
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/vmware-tanzu" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/openshift" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises" class="nav-link active">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

This page details how to configure the Onprem cloud provider for YugabyteDB using the Yugabyte Platform console. If no cloud providers are configured, the main Dashboard page highlights that you need to configure at least one cloud provider.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-0.png)

## Step 1. Configuring the on-premises provider

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
If any of the items from this checklist are true, you need to [provision the nodes manually](#provision-the-nodes-manually).

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

For mount paths, use a mount point with enough space to contain your node density. Use `/data`. If you have multiple drives, add these as a comma-separated list: `/mnt/d0,/mnt/d1`

### Region and Zones

Follow the steps below to provide the location of DB nodes. All these fields are user-defined, which will be later used during the universe creation.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-3.png)

## Step 2. Provision the YugabyteDB nodes

After finishing the cloud provider configuration, click on “Manage Instances” to provision as many nodes as your application requires:

### Manage Instances

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-4.png)

1. Click Add Instances to add the YugabyteDB node. You can use DNS names or IP addresses when adding instances.
1. Instance ID is an optional user-defined identifier.

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-5.png)

### Provision the nodes manually

To provision the nodes manually, you have two options:

* If the SSH user you provided has sudo privileges but requires a password, follow the steps in [Run the pre-provisioning script](#run-the-pre-provisioning-script).

* If the SSH user does not have sudo privileges, follow the steps in [Set up database nodes manually](#set-up-database-nodes-manually).

#### Run the pre-provisioning script

{{< note title="Note" >}}
This step is only required if you set “Manually Provision Nodes” to true; otherwise, you need to skip this step.
{{< /note >}}

Follow these steps for manually provisioning each node by executing the pre-provisioning python script. 

![Configure On-Premises Cloud Provider](/images/ee/onprem/configure-onprem-6.png)

1. Login (ssh) to the Platform virtual machine.
1. Access the docker `yugaware` container.

    ```sh
    $ sudo docker exec -it yugaware bash
    ```

1. Copy/paste the python script prompted in the UI and substitute for a node IP address and mount points. 

    \
    (Optional) Use the `--ask_password` flag if the sudo user requires password authentication.

    ```output
    bash-4.4# /opt/yugabyte/yugaware/data/provision/9cf26f3b-4c7c-451a-880d-593f2f76efce/provision_instance.py --ip 10.9.116.65 --mount_points /data --ask_password
    Executing provision now for instance with IP 10.9.116.65...
    SUDO password:
    ```

1. Wait for the script to finish with the SUCCESS status.

1. Repeat step 3 for every node that will participate in the universe.

You’re finished configuring your on-premises cloud provider, and now you can proceed to universe creation. 

#### Set up database nodes manually

{{< note title="Note" >}}
This step is only required if you set “Manually Provision Nodes” to true; otherwise, you need to skip this step.
{{< /note >}}

If the SSH user configured in the Onprem Provider does not have sudo privileges, then set up each of the database nodes manually by following the steps in this section. Note that you will need access to a user with sudo privileges in order to complete these steps.

##### Time Synchronisation

Database nodes must have time synchronization enabled, therefore a local NTP server or equivalent must be available. For example, ensure an NTP-compatible time service client is installed in the node OS (`chrony` is installed by default in the standard CentOS 7 instance used in this example). Then, configure the time service client to use the available time server. The procedure here includes this step, and assumes chrony is the installed client.

##### Incoming TCP/IP Port Access

Database servers need incoming TCP/IP access enabled to the following ports, for communications between themselves and the Platform server:

| Protocol | Port | Description |
| :------- | :--- | :---------- |
| TCP | 22 | ssh (for automatic administration) |
| TCP | 5433 | YSQL client |
| TCP | 6379 | YEDIS client |
| TCP | 7000 | YB Master Webserver |
| TCP | 7100 | YB Master RPC |
| TCP | 9000 | YB Tserver Webserver |
| TCP | 9042 | YCQL client |
| TCP | 9090 | prometheus server |
| TCP | 9100 | YB Tserver RPC |
| TCP | 9300 | Prometheus node exporter |
| TCP | 12000 | YCQL HTTP (for DB statistics gathering) |
| TCP | 13000 | YSQL HTTP (for DB statistics gathering) |

This table is based on the information on the [default ports page](https://docs.yugabyte.com/latest/reference/configuration/default-ports/).

##### Pre-provision nodes manually

This process carries out all provisioning tasks on the database nodes which require elevated privileges. Once the database nodes have been prepared in this way, the Universe creation process from the Platform server will connect with the nodes only via the `yugabyte` user, and not require any elevation of privileges to deploy and operate the YugabyteDB Universe.

Physical nodes (or cloud instances) are installed with a standard Centos 7 server image. The following steps are to be followed on each physical node, prior to universe creation:

* Log into each database node as a user with sudo enabled (the “centos” user in centos7 images)

* SUDO NEEDED: add the below line to /etc/chrony.conf file:

    ```conf
    server <your-time-server-IP-address> prefer iburst
    ```

    ```sh
    $ sudo chronyc makestep   # (force instant sync to ntp server)
    ```

* SUDO NEEDED: add a new `yugabyte:yugabyte` user and group. Run the following commands:

    ```sh
    $ sudo useradd yugabyte   # (add group yugabyte + create /home/yugabyte)
    $ sudo passwd yugabyte   # (add a password to the yugabyte user)
    $ sudo su - yugabyte   # (change to yugabyte user for convenient execution of next steps)
    ```

* Copy the SSH public key to each DB node. This public key should correspond to the private key entered into the Platform Provider elsewhere in this document.

* Run the following commands as the `yugabyte` user, in home directory after copying the SSH public key file to the user home directory:

    ```sh
    $ mkdir .ssh
    $ chmod 700 .ssh
    $ cat <pubkey file> >> .ssh/authorized_keys
    $ chmod 400 .ssh/authorized_keys
    $ exit   # (exit from the yugabyte user back to previous user)
    ```

* SUDO NEEDED: add following lines to /etc/security/limits.conf:

    ```conf
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

* SUDO NEEDED: modify following line in /etc/security/limits.d/20-nproc.conf:

    ```conf
    *          soft    nproc     12000
    ```

* SUDO NEEDED: Install rsync and OpenSSL packages. Most Linux distributions come with rsync and openssl. If your distribution is missing these packages, install them using the following commands:

    ```sh
    $ sudo yum install openssl
    $ sudo yum install rsync
    ```

    \
    For airgapped environments, make sure your yum repository mirror contains these packages.

* SUDO NEEDED: Tune kernel settings (ONLY if running on a Virtual machine)

    ```sh
    $ sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
    $ sysctl kernel.core_pattern=/home/yugabyte/cores/core_%e.%p >> /etc/sysctl.conf
    ```

* SUDO NEEDED: Prepare and mount the data volume (separate partition for database data):

  * List the available storage volumes with the following command:

      ```sh
      $ lsblk
      ```

  * Perform the following steps for each available volume (all listed volumes other than the root volume): 

      ```sh
      $ sudo mkdir /data   # (or /data1, /data2 etc)
      $ sudo mkdir /data
      $ sudo mkfs -t xfs /dev/nvme1n1   # (create xfs filesystem over entire volume)
      $ sudo vi /etc/fstab
      ```

  * Add the following line to `/etc/fstab`:

      ```conf
      /dev/nvme1n1   /data   xfs   noatime   0   0
      ```

  * Exit from vi, and continue:

    ```sh
    $ sudo mount -av (mounts the new volume using the fstab entry, to validate)
    $ sudo chown yugabyte:yugabyte /data
    $ sudo chmod 755 /data
    ```

##### Install Node Exporter

Download 0.13.0 version of Node exporter if you’re using a Platform version that is older than 2.8.0.0 using the following command:

```sh
$ wget https://github.com/prometheus/node_exporter/releases/download/v0.13.0/node_exporter-0.13.0.linux-amd64.tar.gz
```

Otherwise, download the 1.2.2 version using the following command:

```sh
wget https://github.com/prometheus/node_exporter/releases/download/v1.2.2/node_exporter-1.2.2.linux-amd64.tar.gz
```

If you’re doing an airgapped installation, download Node Exporter using a computer connected to the internet and copy it over to the database nodes.

Copy the `node_exporter-....tar.gz` package file you downloaded into the `/tmp` directory on each of the DB nodes. Ensure this file is readable by the `centos` user on each node (or another user with sudo privileges).

Note that the instructions here are for the 0.13.0 version. The same instructions work with the 1.2.2 version, but make sure to use the right filename.

* SUDO NEEDED: On each DB node (as a user with sudo access) execute the following steps:

    ```sh
    $ sudo mkdir /opt/prometheus
    $ sudo mkdir /etc/prometheus
    $ sudo mkdir /var/log/prometheus
    $ sudo mkdir /var/run/prometheus
    $ sudo mv /tmp/node_exporter-0.13.0.linux-amd64.tar  /opt/prometheus
    $ sudo adduser prometheus (also adds group “prometheus”)
    $ sudo chown -R prometheus:prometheus /opt/prometheus
    $ sudo chown -R prometheus:prometheus /etc/prometheus
    $ sudo chown -R prometheus:prometheus /var/log/prometheus
    $ sudo chown -R prometheus:prometheus /var/run/prometheus
    $ sudo chmod +r /opt/prometheus/node_exporter-0.13.0.linux-amd64.tar
    $ sudo su - prometheus (user session is now as user “prometheus”)
    ```

* Run the following commands as user `prometheus`:

    ```sh
    $ cd /opt/prometheus
    $ tar zxf node_exporter-0.13.0.linux-amd64.tar.gz
    $ exit   # (exit from prometheus user back to previous user)
    $ sudo vi /etc/systemd/system/node_exporter.service
    ```

  * Add the following to the /etc/systemd/system/node_exporter.service file:

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

  * Exit from vi, and continue:

    ```sh
    $ sudo systemctl daemon-reload
    $ sudo systemctl enable node_exporter
    $ sudo systemctl start node_exporter
    ```

* Check the status of the “node_exporter” service with the following command:

    ```sh
    $ sudo systemctl status node_exporter
    ```

##### Install backup utilities

Platform supports backing up YugabyteDB to AWS S3, Azure Storage, Google Cloud Storage, and NFS. Install the backup utility corresponding to the backup storage you plan to use.

**NFS**: Install rsync

Platform uses rsync to do NFS backups, which you installed in an earlier step.

**AWS S3**: Install s3cmd

SUDO NEEDED: Platform relies on s3cmd to support copying backups to AWS S3. Install s3cmd using the following command.

```sh
$ sudo yum install s3cmd
```

Alternatively, for an airgapped install, copy `/opt/third-party/s3cmd-2.0.1.tar.gz` from the Platform node to the database node, and extract it into the `/usr/local` directory on the database node.

```sh
$ cd /usr/local
$ sudo tar xvfz path-to-s3cmd-2.0.1.tar.gz
$ sudo ln -s /usr/local/s3cmd-2.0.1/s3cmd /usr/local/bin/s3cmd 
```

**Azure Storage**: Install azcopy

Download azcopy_linux_amd64_10.4.0.tar.gz using this command:

```sh
$ wget https://azcopyvnext.azureedge.net/release20200410/azcopy_linux_amd64_10.4.0.tar.gz
```

For airgapped installs, copy `/opt/third-party/azcopy_linux_amd64_10.4.0.tar.gz` from the Platform node.

```sh
$ cd /usr/local
$ sudo tar xfz path-to-azcopy_linux_amd64_10.4.0.tar.gz -C /usr/local/bin azcopy_linux_amd64_10.4.0/azcopy --strip-components 1
```

**Google Cloud Storage**: Install gsutil

Download gsutil_4.60.tar.gz using the following command

```sh
$ wget https://storage.googleapis.com/pub/gsutil_4.60.tar.gz
```

For airgapped installs, copy `/opt/third-party/gsutil_4.60.tar.gz` from the Platform node.

```sh
$ cd /usr/local
$ sudo tar xvfz gsutil_4.60.tar.gz
$ sudo ln -s /usr/local/gsutil/gsutil /usr/local/bin/gsutil 
```
