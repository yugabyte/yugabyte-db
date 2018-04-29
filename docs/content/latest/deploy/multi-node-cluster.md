---
title: Private DC
linkTitle: Private DC
description: Private DC
aliases:
  - /deploy/multi-node-cluster/
menu:
  latest:
    identifier: multi-node-cluster
    parent: deploy
    weight: 610
---

Multi-node clusters of YugaByte DB CE can be manually deployed on any cloud provider of choice including major public cloud platforms and private on-premises datacenters.

## Prerequisites

Dedicated hosts or cloud VMs running CentOS 7 with local or remote attached storage are required to run the YugaByte DB. If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- ntp

Here's the command to install these packages.

```{.sh .copy .separator-dollar}
$ sudo yum install -y epel-release ntp
```

## 1. Download and install

### Download

Download the YugaByte CE binary package as described in the [Quick Start section](../../quick-start/install/).

### Install
For the purpose of this document, let's assume that we have 3 instances with private IP addresses as `172.151.17.130, 172.151.17.220, 172.151.17.140` and are accessible from each other over the network. As noted in the [default ports reference](#default-ports-reference) section, YB-Masters will run on port 7100 and YB-TServers will run on port 9100 of these instances. On each of these instances, run the following steps.

Copy the YugaByte DB package into each instace and then running the following commands.

```{.sh .copy .separator-dollar}
$ tar xvfz yugabyte-ce-<version>-<os>.tar.gz && cd yugabyte-<version>/
```

### Configure the installation

- Run the **post_install.sh** script to make some final updates to the installed software.


```{.sh .copy .separator-dollar}
$ ./bin/post_install.sh
```

- YugaByte DB can be configured to use multiple disks that have been previously mounted to the instance. For the purpose of this document, we will create 2 separate directories on the same disk and then use those directories as YugaByte's data directories.

```{.sh .copy .separator-dollar}
$ mkdir /home/centos/disk1 /home/centos/disk2
```

## 2. Start YB-Masters

Execute the following steps on each of the instances. 

- Run `yb-master` as below. Note how multiple directories can be provided to the `--fs_data_dirs` flag. For the full list of flags, see the [yb-master Reference](../../admin/yb-master/). 

```{.sh .copy .separator-dollar}
$ ./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```


- Alternatively, you can also create a `master.conf` file with the following flags and then run the `yb-master` with the `--flagfile` option as shown below.

```{.sh .copy}
--master_addresses=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2 &
```

```{.sh .copy .separator-dollar}
$ ./bin/yb-master --flagfile master.conf &
```

- Make sure all the 3 yb-masters are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the `--fs_data_dirs` flag.

```{.sh .copy .separator-dollar}
$ cat /home/centos/disk1/yb-data/master/logs/yb-master.INFO
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

## 3. Start YB-TServers

Execute the following steps on each of the instances. 

- Run `yb-tserver` as below. Note that all the master addresses have to be provided as a flag. For the full list of flags, see the [yb-tserver Reference](../../admin/yb-tserver/). 

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

- Alternatively, you can also create a `tserver.conf` file with the following flags and then run the `yb-tserver` with the `--flagfile` option as shown below.

```{.sh .copy}
--tserver_master_addrs=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2
```

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver --flagfile tserver.conf &
```

- Make sure all the 3 yb-tservers are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the `--fs_data_dirs` flag.

```{.sh .copy .separator-dollar}
$ cat /home/centos/disk1/yb-data/tserver/logs/yb-tserver.INFO
```

In all the 3 yb-tserver logs, you should see log messages similar to the following.

```sh
I0912 16:27:18.296516  8168 heartbeater.cc:305] Connected to a leader master server at 172.151.17.140:7100
I0912 16:27:18.296794  8168 heartbeater.cc:368] Registering TS with master...
I0912 16:27:18.297732  8168 heartbeater.cc:374] Sending a full tablet report to master...
I0912 16:27:18.298435  8142 client-internal.cc:1112] Reinitialize master addresses from file: ../tserver.conf
I0912 16:27:18.298691  8142 client-internal.cc:1123] New master addresses: 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
I0912 16:27:18.311367  8142 webserver.cc:156] Starting webserver on 0.0.0.0:12000
I0912 16:27:18.311408  8142 webserver.cc:161] Document root: /home/centos/yugabyte/www
I0912 16:27:18.311574  8142 webserver.cc:248] Webserver started. Bound to: http://0.0.0.0:12000/
I0912 16:27:18.311748  8142 rpc_server.cc:158] RPC server started. Bound to: 0.0.0.0:9042
I0912 16:27:18.311828  8142 tablet_server_main.cc:128] CQL server successfully started
```

In the current yb-master leader log, you should see log messages similar to the following.

```sh
I0912 22:26:32.832296  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "766ec935738f4ae89e5ff3ae26c66651" instance_seqno: 1505255192814357 } with Master
I0912 22:26:39.111896  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "9de074ac78a0440c8fb6899e0219466f" instance_seqno: 1505255199069498 } with Master
I0912 22:26:41.055996  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "60042249ad9e45b5a5d90f10fc2320dc" instance_seqno: 1505255201010923 } with Master
```

## 4: Setup Redis service

While the CQL service is turned on by default after all the yb-tservers start, the Redis service is off by default. If you want this cluster to be able to support Redis clients, run the following command from any of the 3 instances. The command below will add the special Redis table into the DB and also start the Redis server on port 6379 on all instances.

```{.sh .copy .separator-dollar}
$ ./bin/yb-admin --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 setup_redis_table
```

## 5: Connect clients

- Clients can connect to YugaByte's CQL service at `172.151.17.130:9042,172.151.17.220:9042,172.151.17.140:9042`

- Clients can connect to YugaByte's Redis service at  `172.151.17.130:6379,172.151.17.220:6379,172.151.17.140:6379`

## Default ports reference

The above deployment uses the various default ports listed below. 

Service | Type | Port 
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
`yb-tserver` | rpc | 9100
`yb-tserver` | admin web server | 9000
`cql` | rpc | 9042
`cql` | admin web server | 12000
`redis` | rpc | 6379
`redis` | admin web server | 11000

