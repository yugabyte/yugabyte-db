---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Deploy 
weight: 30
---

<style>
table {
  float: left;
}
</style>

Multi-node clusters of YugaByte Community Edition can be manually deployed on any cloud provider of choice including major public cloud platforms and private on-premises datacenters.

## Prerequisites

Dedicated hosts or cloud VMs running Centos 7+ with local or remote attached storage are required to run the YugaByte DB. If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- ntp
- cyrus-sasl-plain
- cyrus-sasl-devel

Here's the command to install these packages.

```sh
# install prerequisite packages
$ sudo yum install -y epel-release ntp cyrus-sasl-plain cyrus-sasl-devel
```

## Install

For the purpose of this document, let's assume that we have 3 instances with private IP addresses as `172.151.17.127, 172.151.28.213, 172.151.17.136` and are accessible from each other over the network. On each of these instances, run the following steps.

### Install YugaByte DB  

Copy the YugaByte DB package into each instace and then running the following commands.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte.<version>-centos.tar.gz -C yugabyte
$ cd yugabyte
```

### Configure the installation

- Run the **configure** script to ensure all dependencies get auto-installed. If not already installed, this script will also install a two libraries (`cyrus-sasl` and `cyrus-sasl-plain`) and will request for a sudo password in case you are not running the script as root.


```sh
$ ./bin/configure
```

- Create the directories necessary for the master and tserver to run.

```sh
$ mkdir yb_data yb_data/master yb_data/tserver
$ mkdir yb_data/master/data yb_data/master/wal yb_data/master/logs yb_data/tserver/data yb_data/tserver/wal yb_data/tserver/logs
```

## Start YB-Masters

- On each of the instances, create a `master.conf` file with the following contents.

```sh
--fs_data_dirs=/home/centos/yugabyte/yb_data/master/data
--fs_wal_dirs=/home/centos/yugabyte/yb_data/master/wal
--log_dir=/home/centos/yugabyte/yb_data/master/logs
--webserver_port=7000
--rpc_bind_addresses=0.0.0.0:7100
--use_hybrid_clock False
--webserver_doc_root=/home/centos/yugabyte/www
--create_cluster
--master_addresses=172.151.17.127:7100,172.151.28.213:7100,172.151.17.136:7100
--default_num_replicas=3
```

- Run `yb-master` as below. 

```sh
$ ./bin/yb-master --flagfile master.conf &
```

- Make sure all the 3 yb-masters are now working as expected by inspecting the INFO log.

```sh
$ cat yb_data/master/logs/yb-master.INFO
```

You can see that the 3 yb-masters were able to discover each other and were also able to elect a Raft leader among themselves (the remaining two act as Raft followers).

For the masters that become followers, you will see the following line in the log.
```sh
I0912 16:11:07.419591  8030 sys_catalog.cc:332] T 00000000000000000000000000000000 P bc42e1c52ffe4419896a816af48226bc [sys.catalog]: This master's current role is: FOLLOWER
```

For the master that becomes the leader, you will see the following line in the log.
```sh
I0912 16:11:06.899287 27220 raft_consensus.cc:738] T 00000000000000000000000000000000 P 21171528d28446c8ac0b1a3f489e8e4b [term 2 LEADER]: Becoming Leader. State: Replica: 21171528d28446c8ac0b1a3f489e8e4b, State: 1, Role: LEADER
```

Now we are ready to start the yb-tservers.

## Start YB-TServers

- On each of the instances, create a `tserver.conf` file with the following contents.

```sh
--fs_data_dirs=/home/centos/yugabyte/yb_data/tserver/data
--fs_wal_dirs=/home/centos/yugabyte/yb_data/tserver/wal
--log_dir=/home/centos/yugabyte/yb_data/tserver/logs
--webserver_port=9000
--rpc_bind_addresses=0.0.0.0:9100
--use_hybrid_clock False
--webserver_doc_root=/home/centos/yugabyte/www
--tserver_master_addrs=172.151.17.127:7100,172.151.28.213:7100,172.151.17.136:7100
--memory_limit_hard_bytes=1073741824
--redis_proxy_webserver_port=11000
--redis_proxy_bind_address=0.0.0.0:6379
--cql_proxy_webserver_port=12000
--cql_proxy_bind_address=0.0.0.0:9042
--local_ip_for_outbound_sockets=0.0.0.0
```

- Run `yb-tserver` as below. 

```sh
$ ./bin/yb-tserver --flagfile tserver.conf &
```

- Make sure all the 3 yb-tservers are now working as expected by inspecting the INFO log.

```sh
$ cat yb_data/tserver/logs/yb-tserver.INFO
```

In all the 3 yb-tserver logs, you should see a similar log message.

```sh
I0912 16:27:18.296516  8168 heartbeater.cc:305] Connected to a leader master server at 172.151.17.136:7100
I0912 16:27:18.296794  8168 heartbeater.cc:368] Registering TS with master...
I0912 16:27:18.297732  8168 heartbeater.cc:374] Sending a full tablet report to master...
I0912 16:27:18.298435  8142 client-internal.cc:1112] Reinitialize master addresses from file: ../tserver.conf
I0912 16:27:18.298691  8142 client-internal.cc:1123] New master addresses: 172.151.17.127:7100,172.151.28.213:7100,172.151.17.136:7100
I0912 16:27:18.311367  8142 webserver.cc:156] Starting webserver on 0.0.0.0:12000
I0912 16:27:18.311408  8142 webserver.cc:161] Document root: /home/centos/yugabyte/www
I0912 16:27:18.311574  8142 webserver.cc:248] Webserver started. Bound to: http://0.0.0.0:12000/
I0912 16:27:18.311748  8142 rpc_server.cc:158] RPC server started. Bound to: 0.0.0.0:9042
I0912 16:27:18.311828  8142 tablet_server_main.cc:128] CQL server successfully started
```

## Setup Redis service

While CQL service is turned on by default after all the yb-tservers start, Redis service is off by default. Run the following command from any of the 3 instances in case you want this cluster to be able to support Redis clients. The command below will add the special Redis table into the DB and also start the Redis server on port 6379 on all instances.

```sh
$ ./bin/yb-admin --master_addresses 172.151.17.127:7100,172.151.28.213:7100,172.151.17.136:7100 setup_redis_table
```

## Connect clients

- Clients can connect to YugaByte CQL service at `172.151.17.127:9042,172.151.28.213:9042,172.151.17.136:9042`

- Clients can connect to YugaByte Redis service at  `172.151.17.127:6379,172.151.28.213:6379,172.151.17.136:6379`

## Default ports reference

The above deployment uses the various default ports listed below. 

Service | Type | Port 
--------|------| -------
yb-master | rpc | 7100
yb-master | webserver | 7000
yb-tserver | rpc | 9100
yb-tserver | webserver | 9000
cql | rpc | 9042
cql | webserver | 12000
redis | rpc | 6379
redis | webserver | 11000

