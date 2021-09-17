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

This section describes how to create a YugabyteDB universe with nodes in more than one cloud provider.

## Prerequisites

Before you start creating a universe, ensure that you performed steps applicable to the cloud providers of your choice, as described in [Configure a cloud provider](/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/).

Specifically, you need:

* thing1
* thing2

{{< warning title="notes from Tim start here" >}}
Notes from Tim start here, and go through to the next warning box.
{{< /warning >}}

## Set up the multi-region demo

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

## 2. Examine the universe

Wait for the universe to get created. Note that Yugabyte Platform can manage multiple universes as shown below.

![Multiple universes in Yugabyte Platform console](/images/ee/multi-region-multiple-universes.png)

Once the universe is created, you should see something like the screenshot below in the universe overview.

![Nodes for a Pending Universe](/images/ee/multi-region-universe-overview.png)

### Universe nodes

You can browse to the **Nodes** tab of the universe to see a list of nodes. Note that the nodes are across the different geographic regions.

![Nodes for a Pending Universe](/images/ee/multi-region-universe-nodes.png)

Browse to the cloud provider's instances page. In this example, since we are using Google Cloud Platform as the cloud provider, browse to `Compute Engine` -> `VM Instances` and search for instances that have `helloworld2` in their name. You should see something as follows. It is easy to verify that the instances were created in the appropriate regions.

![Instances for a Pending Universe](/images/ee/multi-region-universe-gcp-instances.png)

## 3. Run a global application

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

## 4. Check the performance characteristics of the app

Recall that we expect the app to have the following characteristics based on its deployment configuration:

* Global consistency on writes, which would cause higher latencies in order to replicate data across multiple geographic regions.
* Low latency reads from the nearest data center, which offers timeline consistency (similar to async replication).

Let us verify this by browse to the **Metrics** tab of the universe in the Yugabyte Platform console to see the overall performance of the app. It should look similar to the screenshot below.

![YCQL Load Metrics](/images/ee/multi-region-read-write-metrics.png)

Note the following:

* Write latency is **139ms** because it has to replicate data to a quorum of nodes across multiple geographic regions.
* Read latency is **0.23 ms** across all regions. Note that the app is performing **100K reads/sec** across the regions (about 33K reads/sec in each region).

It is possible to repeat the same experiment with the `RedisKeyValue` app and get similar results.
