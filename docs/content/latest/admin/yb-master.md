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
---

`yb-master`, located in the bin directory of YugaByte home, is the [YB-Master](../../architecture/concepts/universe/#yb-master) binary.

## Example

```{.sh .copy .separator-dollar}
$ ./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
--replication_factor=3 &
```

## Help 

Use the **-\-help** option to see all the commands supported.

```{.sh .copy .separator-dollar}
$ ./bin/yb-master --help
```

## Config flags

Flag | Mandatory | Default | Description 
----------------------|------|---------|------------------------
`--master_addresses` | Y | N/A |Comma-separated list of all the RPC addresses for `yb-master` consensus-configuration. 
`--fs_data_dirs` | Y | N/A | Comma-separated list of directories where the `yb-master` will place all it's `yb-data/master` data directory. 
`--fs_wal_dirs`| N | Same value as `--fs_data_dirs` | The directory where the `yb-master` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| N | Same value as `--fs_data_dirs`   | The directory to store `yb-master` log files.  
`--rpc_bind_addresses`| N |`0.0.0.0:7100` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_interface`| N |`0.0.0.0` | Address to bind for server UI access.
`--webserver_port`| N | `7000` | Monitoring web server port
`--webserver_doc_root`| N | The `www` directory in the YugaByte DB home directory | Monitoring web server home
`--replication_factor`| N |`3`  | Number of replicas to store for each tablet in the universe.
`--placement_cloud`| N |`cloud1`  | Name of the cloud where this instance is deployed
`--placement_region`| N |`datacenter1`  | Name of the region or datacenter where this instance is deployed
`--placement_zone`| N |`rack1`  | Name of the availability zone or rack where this instance is deployed
`--flagfile`| N | N/A  | Load flags from the specified file.
`--version` | N | N/A | Show version and build info

## Admin UI

The Admin UI for yb-master is available at http://localhost:7000.

### Home 

Home page of the yb-master that gives a high level overview of the cluster. Note all yb-masters in a cluster show exactly the same information.

![master-home](/images/admin/master-home-binary-with-tables.png)

### Tables 

List of tables present in the cluster.

![master-tables](/images/admin/master-tables.png)

### Tablet servers 

List of all nodes (aka yb-tservers) present in the cluster.

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