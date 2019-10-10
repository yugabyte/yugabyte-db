---
title: yb-master
linkTitle: yb-master
description: yb-master
menu:
  latest:
    identifier: yb-master
    parent: admin
    weight: 2440
aliases:
  - admin/yb-master
isTocNested: false
showAsideToc: true
---

`yb-master`, located in the `bin` directory of YugabyteDB home, is the [YB-Master](../../architecture/concepts/universe/#yb-master-process) binary.

## Example

```sh
$ ./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
--replication_factor=3 &
```

## Online help

Run `yb-master --help` to display the online help.

```sh
$ ./bin/yb-master --help
```

## Syntax

```sh
yb-master [ option  ] | [ option ]
```

## Configuration options

### General options

#### --version

Shows version and build information, then exits.

#### --master_addresses

Specifies a comma-separated list of all the RPC addresses for `yb-master` consensus-configuration. 

Mandatory.

#### --fs_data_dirs

Specifies a comma-separated list of directories where the `yb-master` will place all it's `yb-data/master` data directory. 

Mandatory.

#### --fs_wal_dirs

The directory where the `yb-master` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory.

Default: Same value as `--fs_data_dirs`

#### --rpc_bind_addresses

Specifies a comma-separated list of addresses to bind to for RPC connections.

Mandatory.

Default: `0.0.0.0:7100`

#### --flagfile

Specifies the configuration file to load flags from.

#### --server_broadcast_addresses

Specifies the public IP address, or DNS hostname, of the server (along with an optional port).

Default: `0.0.0.0:7100`

#### --webserver_interface

Address to bind for web server user interface access.

Default: `0.0.0.0`

#### --webserver_port

Monitoring web server port.

Default: `7000`

#### --webserver_doc_root

Monitoring web server home.

Default: The `www` directory in the YugabyteDB home directory.

### Logging options

#### --log_dir

The directory to store `yb-master` log files.

Default: Same value as `--fs_data_dirs`

#### --logtostderr

Switches to log to standard error (`stderr`).

### Cluster options

#### --yb_num_shards_per_tserver

Specifies the number of shards per yb-tserver per table when a user table is created. Server automatically picks a valid default internally.

Default: Server automatically picks a valid default internally, typically 8.

#### --max_clock_skew_usec

The expected maximum clock skew between any two nodes in your deployment.

Default: `50000` (50ms)

##### --replication_factor

Number of replicas to store for each tablet in the universe.

Default: `3`

### Placement options

The placement options, or flags, provide 

#### --placement_zone

Name of the availability zone or rack where this instance is deployed.

Default: `rack1`

#### --placement_region

Name of the region or data center where this instance is deployed.

Default: `datacenter1`

#### --placement_cloud

Name of the cloud where this instance is deployed.

Default: `cloud1`

#### --use_private_ip

Determines when to use private IP addresses. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the `placement_*` configuration options.

Default: `never`

## Admin UI

The Admin UI for yb-master is available at http://localhost:7000.

### Home

Home page of the YB-Master service that gives a high level overview of the cluster. Note all YB-Master services in a cluster show identical information.

![master-home](/images/admin/master-home-binary-with-tables.png)

### Tables

List of tables present in the cluster.

![master-tables](/images/admin/master-tables.png)

### Tablet servers

List of all nodes (aka YB-TServer services) present in the cluster.

![master-tservers](/images/admin/master-tservers-list-binary-with-tablets.png)

### Debug

List of all utilities available to debug the performance of the cluster.

![master-debug](/images/admin/master-debug.png)

## Default ports reference

The various default ports are listed below.

Service | Type | Port
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
