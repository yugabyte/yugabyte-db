---
title: Create a multi-cloud universe using Yugabyte Platform
headerTitle: Create a multi-cloud universe
linkTitle: Multi-cloud universe
description: Use Yugabyte Platform to create a YugabyteDB universe that spans multiple cloud providers.
menu:
  latest:
    identifier: create-multi-cloud-universe
    parent: create-deployments
    weight: 35
isTocNested: true
showAsideToc: true
---

This page describes how to create a YugabyteDB universe spanning multiple geographic regions and cloud providers. In this example, you'll deploy a single Yugabyte universe across AWS (US-West-2), Google Cloud (Central-1), and Microsoft Azure (US-East1). 

To do this, you'll need to:

* [Check the prerequisites](#prerequisites)
* [Set up instance VMs](#set-up-instance-vms) in each cloud (AWS, GCP, and Azure)
* [Set up VPC peering](#set-up-vpc-peering) through a VPN tunnel across these 3 clouds
* [Deploy Yugabyte Platform](#deploy-yugabyte-platform) on one of the clouds
* [Deploy a Yugabyte universe](#deploy-a-universe) on your multi-cloud topology
* [Run a global application](#run-a-global-application)

## Prerequisites

* Ability to create VPC, subnets and provision VMs in each of the clouds of choice
* Root access on the instances to be able to create the Yugabyte user
* The instances should have access to the internet to download software (not required, but helpful)

## Set up instance VMs

For on-premises deployments of Yugabyte universes, you need to import nodes that can be managed by Yugabyte Platform. This page outlines the steps required to prepare these YugabyteDB nodes for on-premises deployments.

## Ports

The following ports must be opened for intra-cluster communication (they do not need to be exposed to your application, only to other nodes in the cluster and the platform node):

* 7100 - Master RPC
* 9100 - TServer RPC

The following ports must be exposed for intra-cluster communication, and you should additionally expose these ports to administrators or users monitoring the system, as these ports provide valuable diagnostic troubleshooting and metrics:

* 9300 - Prometheus metrics
* 7000 - Master HTTP endpoint
* 9000 - TServer HTTP endpoint
* 11000 - YEDIS API
* 12000 - YCQL API
* 13000 - YSQL API

The following nodes must be available to your application or any user attempting to connect to the YugabyteDB, in addition to intra-node communication:

* 5433 - YSQL server
* 9042 - YCQL server
* 6379 - YEDIS server

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).

## Preparing nodes

To prepare nodes for on premises deployment:

1. Ensure that the YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](/latest/deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](/latest/deploy/checklist/#running-on-public-clouds). 

1. Install the prerequisites and verify the system resource limits as described in [system configuration](/latest/deploy/manual-deployment/system-config).

1. Ensure you have SSH access to the machine and root access (or the ability to run `sudo`; the sudo user can require a password but having passwordless access is desirable for simplicity and ease of use).

1. Verify that you can SSH into this node (from your local machine if the node has a public address).

    ```sh
    $ ssh -i your_private_key.pem ssh_user@node_ip
    ```

**Do the following with sudo access:**

1. Create the `yugabyte:yugabyte` user + group.

1. Set the home directory to /home/yugabyte.

1. Create the `prometheus:prometheus` user + group.

    {{< tip title="Tip" >}}
If you're using an LDAP directory for managing system users, you can pre-provision Yugabyte and Prometheus users: 

* The `yugabyte` user should belong to the `yugabyte` group.

* Set the home directory for the `yugabyte` user (default /home/yugabyte) and ensure the directory is owned by `yugabyte:yugabyte`. The home directory is used during cloud provider configuration.
    
* The Prometheus username and the group can be user-defined. You enter the custom user during cloud provider configuration.
    {{< /tip >}}

1. Ensure you can schedule Cron jobs with Crontab.

    \
    Cron jobs are used for health monitoring, log file rotation, and cleanup of system core files.

    {{< tip title="Tip" >}}
For any third-party cron scheduling tools, you can disable Crontab and add these cron entries: 

```text
# Ansible: cleanup core files hourly
0 * * * * /home/yugabyte/bin/clean_cores.sh
# Ansible: cleanup yb log files hourly
5 * * * * /home/yugabyte/bin/zip_purge_yb_logs.sh
# Ansible: Check liveness of master
*/1 * * * * /home/yugabyte/bin/yb-server-ctl.sh master cron-check || /home/yugabyte/bin/yb-server-ctl.sh master start
# Ansible: Check liveness of tserver
*/1 * * * * /home/yugabyte/bin/yb-server-ctl.sh tserver cron-check || /home/yugabyte/bin/yb-server-ctl.sh tserver start
```

\
Disabling Crontab creates alerts after the universe is created, but they can be ignored. But you need to ensure cron jobs are set appropriately for the platform to work as expected.
    {{< /tip >}}

1. Verify that Python 2.7 is installed.

1. Enable core dumps and set ulimits:

    ```text
    *       hard        core        unlimited
    *       soft        core        unlimited
    ```

1. Configure SSH as follows:

    * Disable `sshguard`.
    * Set `UseDNS no` in `/etc/ssh/sshd_config` (disables reverse lookup, which is used for auth; DNS is still useable).

1. Set `vm.swappiness` to 0.

1. Set `mount` path permissions to 0755.

## Set up VPC peering

Once you've set up VPC peering through a VPN tunnel, ensure that each node is able to reach all the other nodes in the cluster.

## Deploy Yugabyte Platform

Navigate to Configs > On-Premises Datacenter, and click Edit Provider. On the Provider Info tab... (Let's name this `onprem-provider`)

### Define instance types

On the Instances tab...

For each provider, define an instance type...

### Define regions

On the On-Premises Datacenter tab, click Regions and Zones...

### Add instances

Navigate to Configs > On-Premises Datacenter, and click Manage Instances...

## Deploy a universe





### Get access to the instances

On your local machine, add aliases for convenience, and SSH into the GCP portal machine:

```sh
$ alias gcpportal='ssh -i ~/.yugabyte/yb-dev-aws-2.pem -p 22 centos@10.150.0.14'
$ alias awsportal2='ssh -i ~/.yugabyte/yb-dev-aws-2.pem centos@10.9.6.228 -p 22'
$ gcpportal
```

Now, on the GCP portal, open a shell in the Docker container running the Portal:

```sh
[centos@portal ~]$ sudo docker exec -it yugaware bash
```

Now, in the container:

```sh
$ cd /opt/yugabyte/yugaware/data/keys/
$ ls -alrt
$ cd b41822ff-2f02-4f07-8722-33ad63fa2f79/  # Last directory in the list
$ ls  # REMOVE
$ cat yb-multi-cloud-key.pem  # REMOVE
$ ssh -i yb-multi-cloud-key.pem centos@172.152.63.80  # SSH into what machine?
```

From the **XYZ???** machine, open a shell in the Docker container running PostgreSQL, and open a PostgreSQL shell:

```sh
$ sudo docker exec -it postgres bash
$ psql -U postgres -d yugaware
```

In PostgreSQL, find the region you’re after, and copy its AMI ID:

```sql
select * from region;
```

### Set up the instance

Run the following shell commands **as root**, on each instance:

```sh
mkfs.xfs /dev/nvme1n1
mkdir -p /mnt/d0
mount /dev/nvme1n1 /mnt/d0
```

#### Create the Yugabyte user

Run the following shell commands **as root**, on each instance:

```sh
adduser yugabyte
groupadd yugabyte
usermod -G yugabyte yugabyte
chown yugabyte:yugabyte /mnt/d0
cd ~yugabyte/
mkdir .ssh
#
# NOTE: If copied from the root user, the authorized_keys file contains a
# script at the start. Remove this from the yugabyte user.
#
cp ~/.ssh/authorized_keys  .ssh
chown -R yugabyte:yugabyte .ssh
```

It's easiest to set up the universe if the yugabyte user has sudo access without a password. To do this, edit `/etc/sudoers` (needs root access), and add the following at the end of the file:

```cfg
yugabyte ALL=(ALL) NOPASSWD: ALL
```

In the case of multiple matches, the last match wins. Putting this entry last ensures it wins any conflict.

{{< warning title="END of Tim's notes, START of rough procedures" >}}
The next span, to the following warning box, is a rough outline from other pages like this.
{{< /warning >}}

## Create nodes

Manually create nodes in each cloud provider...

## Create an on-premises provider

Navigate to Configs > On-Premises Datacenter, and click Edit Provider. On the Provider Info tab... (Let's name this `onprem-provider`)

### Define instance types

On the Instances tab...

For each provider, define an instance type...

### Define regions

On the On-Premises Datacenter tab, click Regions and Zones...

### Add instances

Navigate to Configs > On-Premises Datacenter, and click Manage Instances...

## Create a universe

If no universes have been created yet, the Yugabyte Platform Dashboard looks similar to the following:

![Dashboard with No Universes](/images/ee/no-univ-dashboard.png)

To create a multi-cloud universe, do the following:

1. Click Create Universe to create the universe.

    ![Create multi-region universe on GCP](/images/ee/multi-region-create-universe.png)

    \
    The Provider, Regions, and Instance Type fields are initialized based on the [configured cloud providers](../../configure-yugabyte-platform/set-up-cloud-provider/). When you provide the value in the Nodes field, the nodes are automatically placed across all the availability zones to guarantee the maximum availability.

1. Enter the following information:

    * Universe name: `multi-cloud`
    * Set of regions: `us-west`, `us-central`, `us-east`
    * Instance type: `n1-standard-8`

1. Add the following flag for Master and T-Server: `leader_failure_max_missed_heartbeat_periods = 10`.

    \
    Because the the data is globally replicated, RPC latencies are higher. We use this flag to increase the failure detection interval in such a higher RPC latency deployment. See the screenshot below.

1. Click Create.

{{< warning title="END of rough procedures" >}}
Nothing much edited below here, and may not be useful at all?
{{< /warning >}}

## Run a global application

In this section, we are going to connect to each node and perform the following:

* Run the `CassandraKeyValue` workload
* Write data with global consistency (higher latencies because we chose nodes in far away regions)
* Read data from the local data center (low latency, timeline-consistent reads)

Browse to the **Nodes** tab to find the nodes and click **Connect**. This should bring up a dialog showing how to connect to the nodes.

![Multi-region universe nodes](/images/ee/multi-region-universe-nodes-connect.png)

### Connect to the nodes

Create three Bash terminals and connect to each of the nodes by running the commands shown in the popup above. We are going to start a workload from each of the nodes.

On each of the terminals, do the following:

1. Install Java.

    ```sh
    $ sudo yum install java-1.8.0-openjdk.x86_64 -y
    ```

1. Switch to the `yugabyte` user.

    ```sh
    $ sudo su - yugabyte
    ```

1. Export the `YCQL_ENDPOINTS` environment variable.

    \
    Browse to the **Universe Overview** tab in Yugabyte Platform console and click **YCQL Endpoints**. A new tab opens displaying a list of IP addresses.

    \
    Export this into a shell variable on the database node `yb-dev-helloworld1-n1` you connected to. Remember to replace the IP addresses below with those shown in the Yugabyte Platform console.

    ```sh
    $ export YCQL_ENDPOINTS="10.138.0.3:9042,10.138.0.4:9042,10.138.0.5:9042"
    ```

### Run the workload

Run the following command on each of the nodes. Remember to substitute `<REGION>` with the region code for each node.

```sh
$ java -jar /home/yugabyte/tserver/java/yb-sample-apps.jar \
            --workload CassandraKeyValue \
            --nodes $YCQL_ENDPOINTS \
            --num_threads_write 1 \
            --num_threads_read 32 \
            --num_unique_keys 10000000 \
            --local_reads \
            --with_local_dc <REGION>
```

You can find the region codes for each of the nodes by browsing to the **Nodes** tab for this universe in the Yugabyte Platform console. A screenshot is shown below. In this example, the value for `<REGION>` is:

* `us-east4` for node `yb-dev-helloworld2-n1`
* `asia-northeast1` for node `yb-dev-helloworld2-n2`
* `us-west1` for node `yb-dev-helloworld2-n3`

![Region Codes For Universe Nodes](/images/ee/multi-region-universe-node-regions.png)

### Check the performance characteristics of the app

Recall that we expect the app to have the following characteristics based on its deployment configuration:

* Global consistency on writes, which would cause higher latencies in order to replicate data across multiple geographic regions.
* Low latency reads from the nearest data center, which offers timeline consistency (similar to async replication).

You can verify this by browsing to the **Metrics** tab of the universe in the Yugabyte Platform console to see the overall performance of the app. It should look similar to the following screenshot.

![YCQL Load Metrics](/images/ee/multi-region-read-write-metrics.png)

Note the following:

* Write latency is higher because it has to replicate data to a quorum of nodes across multiple regions and providers.
* Read latency is low across all nodes.
