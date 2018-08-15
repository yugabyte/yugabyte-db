---
title: Start YB-TServers
linkTitle: 4. Start YB-TServers
description: Start YB-TServers
aliases:
  - /deploy/manual-deployment/start-tservers
menu:
  1.1-beta:
    identifier: deploy-manual-deployment-start-tservers
    parent: deploy-manual-deployment
    weight: 614
---

{{< note title="Note" >}}
For any cluster, the number of nodes on which the YB-TServers need to be started on **must** equal or exceed the replication factor in order for any table to get created successfully.
{{< /note >}}

As an example scenario, let us assume the following.

- We want to create a a 4 node cluster with replication factor `3`.
      - We would need to run the YB-TServer process on the nodes (`node-a`, `node-b`, `node-c`, `node-d`)
      - Let us assume the master IP addresses are `172.151.17.130`, `172.151.17.220` and `172.151.17.140` (`node-a`, `node-b`, `node-c`)
- We have multiple data drives mounted on `/home/centos/disk1`, `/home/centos/disk2`

This section covers deployment for a single region/zone (or a single datacenter/rack). Execute the following steps on each of the instances.

- Run `yb-tserver` as below. Note that all the master addresses have to be provided as a flag. For the full list of flags, see the [yb-tserver Reference](/admin/yb-tserver/). 

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver \
  --tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
  --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
  >& /home/centos/disk1/yb-tserver.out &
```

- Alternatively, you can also create a `tserver.conf` file with the following flags and then run the `yb-tserver` with the `--flagfile` option as shown below.

```{.sh .copy}
--tserver_master_addrs=172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100
--fs_data_dirs=/home/centos/disk1,/home/centos/disk2
```

```{.sh .copy .separator-dollar}
$ ./bin/yb-tserver --flagfile tserver.conf >& /home/centos/disk1/yb-tserver.out &
```

- Make sure all the 4 yb-tservers are now working as expected by inspecting the INFO log. The default logs directory is always inside the first directory specified in the `--fs_data_dirs` flag.

```{.sh .copy .separator-dollar}
$ cat /home/centos/disk1/yb-data/tserver/logs/yb-tserver.INFO
```

In all the 4 yb-tserver logs, you should see log messages similar to the following.

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

{{< tip title="Tip" >}}Remember to add the command with which you launched `yb-tserver` to a cron to restart it if it goes down.{{< /tip >}}<br>

