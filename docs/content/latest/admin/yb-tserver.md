---
title: yb-tserver
linkTitle: yb-tserver
description: yb-tserver
menu:
  latest:
    identifier: yb-tserver
    parent: admin
    weight: 2450
aliases:
  - admin/yb-tserver
---

`yb-tserver`, located in the bin directory of YugaByte home, is the [YB-TServer](../../architecture/concepts/universe/#yb-tserver) binary.

## Example

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

## Help 

Use the **-\-help** option to see all the commands supported.

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver --help
```

## Config flags

Flag | Mandatory | Default | Description 
----------------------|------|---------|------------------------
`--tserver_master_addrs` | Y | N/A  |Comma-separated list of all the `yb-master` RPC addresses.  
`--fs_data_dirs` | Y | N/A | Comma-separated list of directories where the `yb-tserver` will place it's `yb-data/tserver` data directory. 
`--fs_wal_dirs`| N | Same value as `--fs_data_dirs` | The directory where the `yb-tserver` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| N | Same value as `--fs_data_dirs`   | The directory to store `yb-tserver` log files.  
`--rpc_bind_addresses`| N |`0.0.0.0:9100` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_port`| N | `7000` | Monitoring web server port
`--webserver_doc_root`| N | The `www` directory in the YugaByte DB home directory | Monitoring web server home
`--cql_proxy_bind_address`| N | `0.0.0.0:9042` | YCQL API bind address
`--cql_proxy_webserver_port`| N | 12000 | YCQL metrics monitoring port
`--redis_proxy_bind_address`| N | `0.0.0.0:6379`  | YEDIS API bind address
`--redis_proxy_webserver_port`| N | 11000 | YEDIS metrics monitoring port
`--pgsql_proxy_bind_address`| N | `0.0.0.0:5433`  | PostgreSQL API bind address
`--pgsql_proxy_webserver_port`| N | 13000 | PostgreSQL metrics monitoring port
`--placement_cloud`| N |`cloud1`  | Name of the cloud where this instance is deployed
`--placement_region`| N |`datacenter1`  | Name of the region or datacenter where this instance is deployed
`--placement_zone`| N |`rack1`  | Name of the availability zone or rack where this instance is deployed
`--flagfile`| N | N/A  | Load flags from the specified file.
`--version` | N | N/A | Show version and build info


## Admin UI

The Admin UI for yb-tserver is available at http://localhost:9000.

### Home 

Home page of the yb-tserver that gives a high level overview of this specific instance.

![tserver-home](/images/admin/tserver-home.png)

### Dashboards 

List of all dashboards to review the ongoing operations 

![tserver-dashboards](/images/admin/tserver-dashboards.png)

### Tablets 

List of all tablets managed by this specific instance, sorted by the table name.

![tserver-tablets](/images/admin/tserver-tablets.png)

### Debug

List of all utilities available to debug the performance of this specific instance.

![tserver-debug](/images/admin/tserver-debug.png)


## Default ports reference

The various default ports are listed below. 

Service | Type | Port 
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
`yb-tserver` | rpc | 9100
`yb-tserver` | admin web server | 9000
`ycql` | rpc | 9042
`ycql` | admin web server | 12000
`yedis` | rpc | 6379
`yedis` | admin web server | 11000
`pgsql` | rpc | 5433
`pgsql` | admin web server | 13000
