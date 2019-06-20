---
title: Start YB-TServers
linkTitle: 4. Start YB-TServers
description: Start YB-TServers
aliases:
  - /deploy/manual-deployment/start-tservers
menu:
  latest:
    identifier: deploy-manual-deployment-start-tservers
    parent: deploy-manual-deployment
    weight: 614
isTocNested: true
showAsideToc: true
---

{{< note title="Note" >}}
For any cluster, the number of nodes on which the YB-TServers need to be started on **must** equal or exceed the replication factor in order for any table to get created successfully.
{{< /note >}}

## Example Scenario
Let us assume the following.

- We want to create a a 4 node cluster with replication factor `3`.
      - We would need to run the YB-TServer process on all the 4 nodes say `node-a`, `node-b`, `node-c`, `node-d`
      - Let us assume the master private IP addresses are `172.151.17.130`, `172.151.17.220` and `172.151.17.140` (`node-a`, `node-b`, `node-c`)
- We have multiple data drives mounted on `/home/centos/disk1`, `/home/centos/disk2`

This section covers deployment for a single region/zone (or a single datacenter/rack). Execute the following steps on each of the instances.

## Run yb-tserver with command line params

- Run `yb-tserver` as below. Note that all the master addresses have to be provided as a flag. For each yb-tserver, replace the rpc bind address flags with the private IP of the host running the yb-tserver.

For the full list of flags, see the [yb-tserver Reference](../../../admin/yb-tserver/).

```sh
$ ./bin/yb-tserver \
  --tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --rpc_bind_addresses 172.151.17.130 \
  --start_pgsql_proxy \
  --pgsql_proxy_bind_address=172.151.17.130:5433 \
  --cql_proxy_bind_address=172.151.17.130:9042 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  >& /home/centos/disk1/yb-tserver.out &
```

Add `--redis_proxy_bind_address=172.151.17.130:6379` to the above list if you need to turn on the YEDIS API as well.

{{< note title="Note" >}}
The number of comma seperated values in `tserver_master_addrs` parameter should match the total number of masters (aka replication factor).
{{< /note >}}

## Run yb-tserver with conf file

- Alternatively, you can also create a `tserver.conf` file with the following flags and then run the `yb-tserver` with the `--flagfile` option as shown below. For each yb-tserver, replace the rpc bind address flags with the private IP of the host running the yb-tserver.

```sh
--tserver_master_addrs=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--rpc_bind_addresses=172.151.17.130
--start_pgsql_proxy
--pgsql_proxy_bind_address=172.151.17.130:5433
--cql_proxy_bind_address=172.151.17.130:9042
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2
```

Add `--redis_proxy_bind_address=172.22.25.108:6379` to the above list if you need to turn on the YEDIS API as well.

```sh
$ ./bin/yb-tserver --flagfile tserver.conf >& /home/centos/disk1/yb-tserver.out &
```

## Initialize YSQL

On any yb-tserver or yb-master, run the following command.
```sh
YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 /home/yugabyte/postgres/bin/initdb -D /tmp/yb_pg_initdb_tmp_data_dir -U postgres
```

## Verify Health

- Make sure all the 4 yb-tservers are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the `--fs_data_dirs` flag.

You can do this as shown below.

```sh
$ cat /home/centos/disk1/yb-data/tserver/logs/yb-tserver.INFO
```

In all the 4 yb-tserver logs, you should see log messages similar to the following.

```
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

```
I0912 22:26:32.832296  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "766ec935738f4ae89e5ff3ae26c66651" instance_seqno: 1505255192814357 } with Master
I0912 22:26:39.111896  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "9de074ac78a0440c8fb6899e0219466f" instance_seqno: 1505255199069498 } with Master
I0912 22:26:41.055996  3162 ts_manager.cc:97] Registered new tablet server { permanent_uuid: "60042249ad9e45b5a5d90f10fc2320dc" instance_seqno: 1505255201010923 } with Master
```

{{< tip title="Tip" >}}Remember to add the command with which you launched `yb-tserver` to a cron to restart it if it goes down.{{< /tip >}}<br>
